/**
 @file ctc_greatbelt_vlan.h

 @date 2009-10-17

 @version v2.0

 This file is to define ctc basic vlan and advanced vlan APIs. The VLAN management API provides Basic vlan, vlan class and vlan mapping function.
 \p
 First, the Centec Greatbelt VLAN API provides the capability to manage VLAN definitions within the Centec Greatbelt switch ASIC.
 Each VLAN associates an integer identifier with a group of ports which should be treated as a broadcast domain.
 You should create vlan by API ctc_vlan_create_vlan(). And API ctc_vlan_destroy_vlan() is used to destroy the vlan created before.
 Basic VLAN configuration allows for the creation and destruction of 802.1Q VLAN definitions.
 This allows for binding an 802.1Q VLAN ID to a set of ports and managing the member ports.
 The port membership definitions are defined as a bitmap of ports by API ctc_vlan_add_port().
 Then, you can configuration receive and transmit of the vlan. By default, when vlan is create, receive and transmit of the vlan is enabled,
 and fid is equal to vlan_id.
 \p
 Furthermore, the Centec Greatbelt VLAN APIs provides the capability of manage advanced vlan feature by vlan class and vlan mapping function.
 \p
 The Centec Greatbelt VLAN APIs can support vlan class by:
 \p
 \d    policy--flex in vlan class structure;
 \d    mac--just macsa or macda;
 \d    ip--just ipv4sa or ipv6sa;
 \d    protocol--ether-type;
 \d    port based vlan class, it can be supported by default vlan configuration on port.
 \p
 In Centec Greatbelt vlan class PRI: mac/ipv4/ipv6/policy > protocol > port(default).
 \p
 Vlan class function refer to ctc_vlan_add_vlan_class() and ctc_vlan_remove_vlan_class(). And it can configure default vlan class
 action if mismatch any entry, by ctc API ctc_vlan_add_default_vlan_class().
 \p
 The Centec Greatbelt VLAN APIs can support vlan mapping in direction ingress or egress.
 Ingress vlan mapping support the following application:
 \p
 \d    QinQ
 \d    vlan translation
 \d    vlan stacking
 \d    vlan switching.
 \p
 The ingress vlan mapping function refer to ctc APIs ctc_vlan_add_vlan_mapping() and ctc_vlan_update_vlan_mapping(), ctc_vlan_remove_vlan_mapping().
 And it can configure default ingress vlan mapping action if mismatch any entry, by ctc_vlan_add_default_vlan_mapping().
 \p
 Centec Greatbelt VLAN APIs can support vlan tag(s or c) /vlan id /cos action type in ingress vlan mapping as following:
 \p
 \t  |   stag action  |     NONE    |  REP_OR_ADD |     APPEND   |     DEL     |
 \t  |    svid:scos   |             |             |              |             |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:none   |      1      |      1      |       1      |      1      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  none:default  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  swap:default  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |   new:default  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:none  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:swap  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:new   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |default:default |      0      |      1      |       1      |      0      |
 \p
 \p
 \t  |   ctag action  |     NONE    |  REP_OR_ADD |     APPEND   |     DEL     |
 \t  |    cvid:ccos   |             |             |              |             |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:none   |      1      |      1      |       1      |      1      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  none:default  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  swap:default  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |   new:default  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:none  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:swap  |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:new   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |default:default |      0      |      1      |       1      |      0      |
 \p
 Egress vlan mapping support the following application: QinQ, vlan translation. The egress vlan mapping function refer to add/remove entry by ctc API
 ctc_vlan_add_egress_vlan_mapping() and ctc_vlan_remove_egress_vlan_mapping(). And it can configure default egress vlan
 mapping action if mismatch any entry, by ctc_vlan_add_default_egress_vlan_mapping().
 \p
 Centec Greatbelt VLAN APIs can support vlan tag(s or c) /vlan id /cos action type in egress vlan mapping as following:
 \p
 \t  |   stag action  |     NONE    |  REP_OR_ADD |     APPEND   |     DEL     |
 \t  |    svid:scos   |             |             |              |             |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:none   |      1      |      1      |       1      |      1      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  none:mapped   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  swap:mapped   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |   new:mapped   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:none  |      0      |      0      |       0      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:swap  |      0      |      0      |       0      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:new   |      0      |      0      |       0      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  | default:mapped |      0      |      0      |       0      |      0      |
 \p
 \p
 \t  |   ctag action  |     NONE    |  REP_OR_ADD |     APPEND   |     DEL     |
 \t  |    cvid:ccos   |             |             |              |             |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:none   |      1      |      1      |       1      |      1      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    none:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  none:mapped   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |    swap:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  swap:mapped   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:none   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:swap   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |     new:new    |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |   new:mapped   |      0      |      1      |       1      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:none  |      0      |      0      |       0      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:swap  |      0      |      0      |       0      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  |  default:new   |      0      |      0      |       0      |      0      |
 \t  |----------------|-------------|-------------|------------- |-------------|
 \t  | default:mapped |      0      |      0      |       0      |      0      |
 \p
 The Centec SDK layer maintains consistency between Basic vlan configuration and VLAN class configuration, Vlan mapping configuration.
 \p

*/

#ifndef _CTC_GREATBELT_VLAN_H
#define _CTC_GREATBELT_VLAN_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_vlan.h"
#include "ctc_const.h"
#include "ctc_parser.h"
#include "ctc_stats.h"
/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
/**
 @addtogroup vlan VLAN
 @{
*/

/**
 @brief Init the vlan module

 @param[in] lchip    local chip id

 @param[in] vlan_global_cfg         vlan module global config

 @remark
        Initialize device tables and driver structures for VLAN management functions:
        \p
        (1)basic vlan
        \p
        (2)adv-vlan: vlan class and vlan mapping.
        \p
        By default, all vlan transmit and receive is disable.
        Arp packet and dhcp packet is discard.
        0~59 port is tagged for all VLAN.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_init(uint8 lchip, ctc_vlan_global_cfg_t* vlan_global_cfg);

/**
 @brief De-Initialize vlan module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the vlan configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_deinit(uint8 lchip);

/**
 @brief The function is to create a vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @remark
        Allocate and configure a VLAN on the Centec device.
        Create a new VLAN with the given vlan_id.
        By default, transmit and receive of this vlan is enable, and fid is equal to vlan_id.
        Arp packet and dhcp packet is discard.
        0~59 port is tagged for this VLAN.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_create_vlan(uint8 lchip, uint16 vlan_id);

/**
 @brief  Create a user vlan

 @param[in] lchip    local chip id

 @param[in] user_vlan         user vlan info

 @remark
        Allocate and configure a VLAN on the Centec device.
        Create a new VLAN with the given vlan_id, given vlanptr and given fid(vsi).
        By default, transmit and receive of this vlan is enable.
        Arp packet and dhcp packet is forward.
        0~59 port is tagged for this VLAN.
        L2 flooding entry added by default, fid is l2mc group id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_create_uservlan(uint8 lchip, ctc_vlan_uservlan_t* user_vlan);
/**
 @brief The function is to remove the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @remark
        To deallocate the VLAN previously created, API ctc_vlan_destroy_vlan must be used.
        By default, this vlan transmit and receive is disable.
        Arp packet and dhcp packet is discard.
        0~59 port is tagged for all VLAN.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_destroy_vlan(uint8 lchip, uint16 vlan_id);
/**
 @brief  Add member ports to a vlan

 @param[in] lchip_id    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] port_bitmap     bits map of port in vlan

 @remark
        Adds the selected ports to the VLAN. At the same time allow vlan on the specify ports.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_ports(uint8 lchip_id, uint16 vlan_id, ctc_port_bitmap_t port_bitmap);
/**
 @brief The function is to add member port to a vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] gport           Global port of the local chip

 @remark
        Adds the selected ports to the VLAN.
        When vlan filting enabled on the port, the port with forwarding vlan as this vlan_id will be forwarding.
        If you want use vlan filtering function, it is referring to API ctc_port_set_vlan_filter_en().

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_port(uint8 lchip, uint16 vlan_id, uint32 gport);

/**
 @brief The function is to show vlan's member port

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] gchip           global chip id

 @param[out] port_bitmap    bits map of port in vlan

 @remark
        Get info about whether ports are added in the given vlan_id VLAN.
        *port_bitmap is the bitmap of 0~59 port in this vlan_id.
        If the bit of the port is set, it means that the port is added in the vlan;
        otherwise, the port is not added.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_get_ports(uint8 lchip, uint16 vlan_id, uint8 gchip, ctc_port_bitmap_t port_bitmap);
/**
 @brief  Remove member ports to a vlan

 @param[in] lchip_id    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] port_bitmap          bits map of port in vlan

 @remark
        Removes the specified ports from the given VLAN. If the requested ports is not already
        members of the VLAN, the routine returns CTC_E_MEMBER_PORT_NOT_EXIST.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_ports(uint8 lchip_id, uint16 vlan_id, ctc_port_bitmap_t port_bitmap);
/**
 @brief The function is to remove member port to a vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] gport           Global port of the local chip

 @remark
        Removes the specified ports from the given VLAN. If the requested ports is not already
        members of the VLAN, the routine returnsCTC_E_MEMBER_PORT_NOT_EXIST

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_port(uint8 lchip, uint16 vlan_id, uint32 gport);

/**
 @brief The function is to set tagged port on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] gport           Global port of the local chip

 @param[in] tagged          Vlan allow tagged on this port

 @remark
        Set whether the port is tagged or not in this vlan.
        If you want to configure output packet is ctagged, stagged, double tagged or untag, it is referring to
        API ctc_port_set_dot1q_type, type is mode of output packet.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_set_tagged_port(uint8 lchip, uint16 vlan_id, uint32 gport, bool tagged);

/**
 @brief The function is to get tagged port on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] gchip           global chip id

 @param[out] port_bitmap     Bitmap of port tagged or not in this vlan

 @remark
        Set info about whether the port is tagged or not in this vlan.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_get_tagged_ports(uint8 lchip, uint16 vlan_id, uint8 gchip, ctc_port_bitmap_t port_bitmap);

/**
 @brief The function is to set receive enable on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean value denote receive is enable

 @remark
        If enable is set TRUE, receive packet will be enable in this vlan_id.
        Otherwise, it is disable in this vlan_id.
        When a vlan is created, receive is enable in the vlan by default.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_set_receive_en(uint8 lchip, uint16 vlan_id, bool enable);

/**
 @brief The function is to get receive on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean value denote receive is enable

 @remark
        Get info about whether receive is enable or not in this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_receive_en(uint8 lchip, uint16 vlan_id, bool* enable);

/**
 @brief The function is to set tranmit enable on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean value denote transmit is enable

 @remark
        If enable is set TRUE, transmit packet will be enable in this vlan_id.
        Otherwise, it is disable in this vlan_id.
        When a vlan is created, transmit is enable in the vlan by default.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_set_transmit_en(uint8 lchip, uint16 vlan_id, bool enable);

/**
 @brief The function is to get tranmit on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean value denote transmit is enable

 @remark
        Get info about whether transmit is enable or not in this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_transmit_en(uint8 lchip, uint16 vlan_id, bool* enable);

/**
 @brief The function is to set bridge enable on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean value denote bridge is enable

 @remark
        If set TRUE, bridging is enabled on this VLAN.
        Otherwise, it is disable in this vlan_id.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_bridge_en(uint8 lchip, uint16 vlan_id, bool enable);

/**
 @brief The function is to get bridge on vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean value denote bridge is enable

 @remark
        Get info about whether bridge is enable or not in this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_bridge_en(uint8 lchip, uint16 vlan_id, bool* enable);

/**
 @brief The fucntion is to set vrfid of vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] fid             Fid used in bridge

 @remark
     VLAN mapping to FID in 802.1D VLAN bridge
     \p
     1. One to one mapping of VLAN ID and FID in IVL bridge ,(VLAN:FID = 1:1)
     \p
     2. many to one mapping of VLAN ID and FID in SVL bridge

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_fid(uint8 lchip, uint16 vlan_id, uint16 fid);

/**
 @brief The fucntion is to get vrfid of vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[out] fid            Fid used in bridge

 @remark
        Get info about fid of the VLAN given used in bridge

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_fid(uint8 lchip, uint16 vlan_id, uint16* fid);

/**
 @brief The function is set mac learning enable/disable on the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable          A boolean val denote learning enable/disable

 @remark
        If set TRUE, learning in this vlan is enabled.
        Otherwise, it is disabled in this vlan.
        When a vlan is created, learning is enable in this vlan_id by default.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_learning_en(uint8 lchip, uint16 vlan_id, bool enable);

/**
 @brief The function is get mac learning enable/disable on the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[out] enable         A boolean val denote learning enable/disable

 @remark
        Get info about whether learning is enabled or not in this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_learning_en(uint8 lchip, uint16 vlan_id, bool* enable);

/**
 @brief The function is to set igmp snooping enable on the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[out] enable         A boolean val denote learning enable/disable

 @remark
        If set TRUE, igmp snooping in this vlan is enabled.
        Otherwise, it is disabled in this vlan.
        When a vlan is created, igmp snooping is disable in this vlan_id by default.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_igmp_snoop_en(uint8 lchip, uint16 vlan_id, bool enable);

/**
 @brief The function is to get igmp snooping enable of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[out] enable         A boolean val denote learning enable/disable

 @remark
        Get info about whether igmp snooping is enabled or not in this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_igmp_snoop_en(uint8 lchip, uint16 vlan_id, bool* enable);

/**
 @brief The function is to set dhcp exception action of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] type            exception type, refer to data structure of ctc_exception_type_s

 @remark
        Dhcp packet forwarding with this vlan_id will do action just as enum ctc_exception_type_t.
        When a vlan created, dhcp packet will be discard and not to cpu by default.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_dhcp_excp_type(uint8 lchip, uint16 vlan_id, ctc_exception_type_t type);

/**
 @brief The function is to get dhcp exception action of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] type            exception type, refer to data structure of ctc_exception_type_s

 @remark
        Get info about which mode of action will dhcp packet forwarding with this vlan_id do.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_dhcp_excp_type(uint8 lchip, uint16 vlan_id, ctc_exception_type_t* type);

/**
 @brief The function is to set arp exception action of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] type            exception type, refer to data structure of ctc_exception_type_s

 @remark
        Arp packet forwarding with this vlan_id will do action just as enum ctc_exception_type_t.
        When a vlan created, arp packet will be discard and not to cpu by default.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_arp_excp_type(uint8 lchip, uint16 vlan_id, ctc_exception_type_t type);

/**
 @brief The function is to get arp exception action of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] type            exception type, refer to data structure of ctc_exception_type_s

 @remark
        Get info about which mode of action will arp packet forwarding with this vlan_id do.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_arp_excp_type(uint8 lchip, uint16 vlan_id, ctc_exception_type_t* type);

/**
 @brief The function is to set various properties of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] vlan_prop       vlan property

 @param[in] value           value need set

 @remark
        Set property configuration as enum ctc_vlan_property_t with the uint32 value in the VLAN given.
        \p
        vlan_prop:
        \p
        CTC_VLAN_PROP_RECEIVE_EN:                vlan receive enabel.
        \p
        CTC_VLAN_PROP_BRIDGE_EN:                 vlan bridge enabel.
        \p
        CTC_VLAN_PROP_TRANSMIT_EN:              vlan transmit enabel.
        \p
        CTC_VLAN_PROP_LEARNING_EN:               vlan learning enabel.
        \p
        CTC_VLAN_PROP_DHCP_EXCP_TYPE:            dhcp packet processing type.
        \p
        CTC_VLAN_PROP_ARP_EXCP_TYPE:             arp packet processing type.
        \p
        CTC_VLAN_PROP_IGMP_SNOOP_EN:             igmp snooping enable.
        \p
        CTC_VLAN_PROP_FID:                       fid for general l2 bridge, VSIID for L2VPN.
        \p
        CTC_VLAN_PROP_IPV4_BASED_L2MC:           Ipv4 based l2mc.
        \p
        CTC_VLAN_PROP_IPV6_BASED_L2MC:           Ipv6 based l2mc.
        \p
        CTC_VLAN_PROP_PTP_EN:                    ptp enable.
        \p
        CTC_VLAN_PROP_DROP_UNKNOWN_UCAST_EN:     vlan drop unknown ucast packet enable.
        \p
        CTC_VLAN_PROP_DROP_UNKNOWN_MCAST_EN:     vlan drop unknown mcast packet enable.
        \p
        When a vlan is created, by default, unknown ucast packet and drop unknown mcast packet forwarding with this vlan_id is not to dropped.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_property(uint8 lchip, uint16 vlan_id, ctc_vlan_property_t vlan_prop, uint32 value);

/**
 @brief The function is to get various properties of the vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] vlan_prop       vlan property

 @param[in] value           value need get

 @remark
        Get info about property configuration of this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_property(uint8 lchip, uint16 vlan_id, ctc_vlan_property_t vlan_prop, uint32* value);

/**
 @brief The function is to set various properties of the vlan with direction

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] vlan_prop       vlan property with direction

 @param[in] dir             direction

 @param[in] value           set_value

 @remark
        Set property configuration as enum ctc_vlan_direction_property_t with the uint32 value in the VLAN given.
        \p
        vlan_prop:
        \p
            CTC_VLAN_DIR_PROP_ACL_EN:                vlan acl enable, refer CTC_ACL_EN_XXX
            \p
            CTC_VLAN_DIR_PROP_ACL_IPV6_EN:           vlan ipv6 enable, refer CTC_ACL_IPV6_EN_XXX
            \p
            CTC_VLAN_DIR_PROP_ACL_CLASSID:           vlan classid
            \p
            CTC_VLAN_DIR_PROP_ACL_ROUTED_PKT_ONLY:   vlan routed packet only
            \p
            CTC_VLAN_DIR_PROP_STATS_CLEAR:           clear vlan stats

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_set_direction_property(uint8 lchip, uint16 vlan_id, ctc_vlan_direction_property_t vlan_prop, ctc_direction_t dir, uint32 value);

/**
 @brief The function is to get various properties of the vlan with direction

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] vlan_prop       vlan property with direction

 @param[in] dir             direction

 @param[in] value           get_value

 @remark
        Get info about property configuration of this vlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_direction_property(uint8 lchip, uint16 vlan_id, ctc_vlan_direction_property_t vlan_prop, ctc_direction_t dir, uint32* value);

/**
 @addtogroup VlanClassification VlanClassification
 @{
*/

/**
 @brief The function is to add one vlan classification rule

 @param[in] lchip    local chip id

 @param[in] p_vlan_class    Vlan classification info, include key and policy

 @remark
     Add an association from MAC, IPV4 subnet, IPV6 subnet, POLICY or PROTOCOL to use for assigning a vlan tag to untag or priority packet.
     Type in p_vlan_class will be the mode of vlan class:
     \p
     1. vlan class by policy(packet feature) according to :
     \p
     (1) flex_ipv4:ipv4sa|ipv4da|macsa|macda|l3-type|l4-type|l4-src-port|l4-dest-port
     \p
     (2) flex_ipv6:ipv6sa|ipv6da|macsa|macda|l4-type|l4-src-port|l4-dest-port
     \p
     if policy used, set flag CTC_VLAN_CLASS_FLAG_USE_FLEX.
     \p
     2. vlan class by mac according to :
     \p
     mac:macsa or macda (if macda, set flag CTC_VLAN_CLASS_FLAG_USE_MACDA)
     \p
     3. vlan class by ip according to :
     \p
     (1) ipv4:ipv4_sa
     \p
     (2) ipv6:ipv6_sa
     \p
     4. vlan class by protocol according to :
     \p
     ether-type: the ether type is a 16-bit value that matches the ethertype field in the received packet.
     \p
     Furthermore, PRI: mac/ipv4/ipv6/policy > protocol > port(default).
     You can configure vlan_id as the new vlan id. If output new cos, flag CTC_VLAN_CLASS_FLAG_OUTPUT_COS must be set.
     Enable the function on port by API ctc_port_set_scl_key_type() with key type relative.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_vlan_class(uint8 lchip, ctc_vlan_class_t* p_vlan_class);

/**
 @brief The function is to remove on vlan classification rule

 @param[in] lchip    local chip id

 @param[in] p_vlan_class    Vlan classification info, include key and policy

 @remark
     Remove an association from MAC, IPV4 subnet, IPV6 subnet, POLICY or PROTOCOL to vlan.
     Type in p_vlan_class will be the mode of vlan class:
     \p
     1. vlan class by policy(packet feature) according to :
     \p
     (1) flex_ipv4:ipv4sa|ipv4da|macsa|macda|l3-type|l4-type|l4-src-port|l4-dest-port
     \p
     (2) flex_ipv6:ipv6sa|ipv6da|macsa|macda|l4-type|l4-src-port|l4-dest-port
     \p
     if policy used, set flag CTC_VLAN_CLASS_FLAG_USE_FLEX.
     \p
     2. vlan class by mac according to :
     \p
     mac:macsa or macda (if macda, set flag CTC_VLAN_CLASS_FLAG_USE_MACDA)
     \p
     3. vlan class by ip according to :
     \p
     (1) ipv4:ipv4_sa
     \p
     (2) ipv6:ipv6_sa
     \p
     4. vlan class by protocol according to :
     \p
     ether-type: the ether type is a 16-bit value that matches the ethertype field in the received packet.
     \p
     Disable the function on port by API ctc_port_set_scl_key_type() with key type relative.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_vlan_class(uint8 lchip, ctc_vlan_class_t* p_vlan_class);

/**
 @brief The function is to flush vlan classification by type

 @param[in] lchip    local chip id

 @param[in] type    Vlan classification type, like mac-based or ip-based.

 @remark
     Remove all associations from MAC, IPV4, IPV6 or PROTOCOL to vlan for vlan class function
     \p
     (1)Remove all associations from IPV4 addresses to VLAN.
     \p
     (2)Remove all associations from IPV6 addresses to VLAN.
     \p
     (3)Remove all associations from MAC addresses to VLAN.
     \p
     (4)Remove all associations from PROTOCOL addresses to VLAN

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_all_vlan_class(uint8 lchip, ctc_vlan_class_type_t type);

/**
 @brief Add vlan class default entry per label

 @param[in] lchip    local chip id

 @param[in] type            Vlan class type, mac-based ipv4-based and ipv6-based

 @param[in] p_action        Default entry action

 @remark
     Configure vlan class default action, if not matching any entry for MAC, IPV4, IPV6 to vlan, the packet will do this action
     as flag in struct ctc_vlan_miss_t
     Type will be the mode of vlan class, see enum ctc_vlan_class_type_t.
     By default, do nothing will be the action.
 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_default_vlan_class(uint8 lchip, ctc_vlan_class_type_t type, ctc_vlan_miss_t* p_action);
/**
 @brief Remove vlan class default entry per label

 @param[in] lchip    local chip id

 @param[in] type            Vlan class type, mac-based ipv4-based and ipv6-based

 @remark
    Recover configuration of vlan class default action to do nothing
    Type will be the mode of vlan class, see enum ctc_vlan_class_type_t.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_default_vlan_class(uint8 lchip, ctc_vlan_class_type_t type);

/**@} end of @addtogroup VlanClassification*/

/**
 @addtogroup VlanMapping VlanMapping
 @{
*/

/**
 @brief The function is to create a vlan range

 @param[in] lchip    local chip id

 @param[in] vrange_info   vlan range info

 @param[in] is_svlan      vlan range used for svlan

 @remark
      Create vlan range group including three element: vrange_grpid, direction (see struct ctc_vlan_range_info_t) and is_svlan.
      is_svlan denote whether the vlan range group property svlan or cvlan.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_create_vlan_range_group(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool is_svlan);

/**
 @brief The function is to get vlan range type

 @param[in] lchip    local chip id

 @param[in] vrange_info   vlan range info

 @param[out] is_svlan      vlan range used for svlan

 @remark
     Get info about whether the vlan range group is svlan or not, according to two elements: vrange_grpid and direction of the vlan range group.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_vlan_range_type(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool* is_svlan);

/**
 @brief The function is to delete a vlan range

 @param[in] lchip    local chip id

 @param[in] vrange_info   vlan range info

 @remark
     Delete vlan range group with two element: vrange_grpid and direction.
     Then 8 entries in that vlan range group will be reset.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_destroy_vlan_range_group(uint8 lchip, ctc_vlan_range_info_t* vrange_info);

/**
 @brief The function is to add a vlan range member to a vrange_group

 @param[in] lchip    local chip id

 @param[in] vrange_info   vlan range info

 @param[in] vlan_range    vlan_start and vlan_end in vlan range

 @remark
      Add vlan range entry member into one vlan range group, according to vrange_grpid & direction of the vlan range group.
      \p
      Note: 1. vlan range group must be create before this, or will reply error.
      \p
            2. one vlan range group only can have 8 vlan range members.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range);

/**
 @brief The function is to remove a vlan range member to a vrange_group

 @param[in] lchip    local chip id

 @param[in] vrange_info   vlan range info

 @param[in] vlan_range    vlan_start and vlan_end in vlan range

 @remark
      Remove vlan range entry from vlan range group, according to vrange_grpid & direction of the vlan range group.
      Note: vlan range group must be create and this vlan range entry must be added before, or will reply error.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range);

/**
 @brief The function is to get all vlan range members from a vlan range

 @param[in] lchip    local chip id

 @param[in]  vrange_info   vlan range info

 @param[out] vrange_group    all vlan_start vlan_end in a group

 @param[out] count      valid range count

 @remark
      Get info about vlan range entries and count in the vlan range group,according to vrange_grpid & direction

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_group_t* vrange_group, uint8* count);

/**
 @brief The function is to add one vlan mapping entry on the port in IPE

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Vlan mapping info, include key and mapped action

 @remark
       Configure ingress vlan mapping on gport, expecting packet received from this gport(with some special vid or vrange_grpid or cos or not) to do special action
       Key type(any combination) will be the mapping key, see key in struct ctc_vlan_mapping_t.
       If not using cvid/svid/scos/ccos as key, CTC_VLAN_MAPPING_KEY_NONE will be set(only source port as key).
       If any entry is matched, it will do the action as action in struct ctc_vlan_mapping_t.
       \p
       Except the key and action you must set, you must configure as follow:
       \p
       1. stag_op(stag special action) & ctag_op(ctag special action) will be set configuration to stag & ctag, seeing enum ctc_vlan_tag_op_t.
       \p
       2. svid_sl(svid special action) & scos_sl(scos special action) & cvid_sl(cvid special action) & ccos_sl(ccos special action)  will be set configuration to
       svlan id, scos, cvlan id, ccos, seeing enum ctc_vlan_tag_sl_t.
       \p
       for example:
       \p
       ingress vlan translation(for example:s+c->s'+c'):
       \p
           (1)stag_op:CTC_VLAN_TAG_OP_REP_OR_ADD;
       \p
           (2)ctag_op:CTC_VLAN_TAG_OP_REP_OR_ADD;
       \p
           (3)svid_sl:CTC_VLAN_TAG_SL_NEW;
       \p
           (4)cvid_sl:CTC_VLAN_TAG_SL_NEW;
       \p
       QinQ(c->c+s):
       \p
           (1)stag_op:CTC_VLAN_TAG_OP_REP_OR_ADD
       \p
           (2)svid_sl:CTC_VLAN_TAG_SL_NEW, new svid must be configured (if having no svid, 0 is used);
       \p
       vlan stacking(s->s+s'):
       \p
           (1)stag_op:CTC_VLAN_TAG_OP_ADD;
       \p
           (2)svid_sl:CTC_VLAN_TAG_SL_NEW, new svid must be configured (if having no svid, 0 is used);

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_vlan_mapping(uint8 lchip, uint32 gport, ctc_vlan_mapping_t* p_vlan_mapping);

/**
 @brief The function is to get one vlan mapping entry

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Vlan mapping info

 @remark
       Get one vlan mapping entry based on the given key.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_vlan_mapping(uint8 lchip, uint32 gport, ctc_vlan_mapping_t* p_vlan_mapping);

/**
 @brief The function is to update one vlan mapping entry on the port in IPE

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Vlan mapping info

 @remark
       Update configuration ingress vlan mapping on gport.
       How to use it is the same as API ctc_vlan_add_vlan_mapping().


 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_update_vlan_mapping(uint8 lchip, uint32 gport, ctc_vlan_mapping_t* p_vlan_mapping);

/**
 @brief The function is to remove one vlan mapping entry on the port in IPE

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Vlan mapping info, include key info to be deleted

 @remark
       Delete configuration ingress vlan mapping on gport(with some special vid or vrange_grpid or cos or not).
       Key type(any combination) will be the mapping key, see key in struct ctc_vlan_mapping_t.
       If not using cvid/svid/scos/ccos as key, CTC_VLAN_MAPPING_KEY_NONE will be set(only source port as key).
       If any entry is matched, it will do the action as action in struct ctc_vlan_mapping_t.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_vlan_mapping(uint8 lchip, uint32 gport, ctc_vlan_mapping_t* p_vlan_mapping);

/**
 @brief The function is to add vlan mapping default entry on the port

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_action        Default entry action

 @remark
      Configure ingress vlan mapping default action on gport for packet received from this gport(direction ingress).
      If not matching any entry,packet will do this action just as flag in struct ctc_vlan_miss_t.
      By default, if no configure default action, packet will do nothing.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_default_vlan_mapping(uint8 lchip, uint32 gport, ctc_vlan_miss_t* p_action);

/**
 @brief The function is to remove vlan mapping miss match default entry of port

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @remark
      Recover configuration of ingress vlan mapping default action to do nothing on gport

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_default_vlan_mapping(uint8 lchip, uint32 gport);

/**
 @brief The function is to remove all vlan mapping entries on port

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @remark
      Remove all ingress vlan mapping configuration for this gport(direction ingress).

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_all_vlan_mapping_by_port(uint8 lchip, uint32 gport);

/**@} end of @addtogroup VlanMapping*/

/**
 @addtogroup EgressVlanEdit EgressVlanEdit
 @{
*/

/**
 @brief The function is to add one egress vlan mapping entry on the port in EPE

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Egress vlan mapping info, include key and mapped action

 @remark
       Configure egress vlan mapping on gport, expecting packet transmitted from this gport(with some special vid or vrange_grpid or cos or not) to do special action
       Key type(any combination) will be the egress mapping key, see key in struct ctc_egress_vlan_mapping_t.
       If not using cvid/svid/scos/ccos as key, CTC_EGRESS_VLAN_MAPPING_KEY_NONE will be set(only source port as key).
       If any entry is matched, it will do the action as action in struct ctc_egress_vlan_mapping_action_t.
       \p
       Except the key and action you must set, you must configure as follow:
       \p
       1. stag_op(stag special action) & ctag_op(ctag special action) will be set configuration to stag & ctag, seeing enum ctc_vlan_tag_op_t.
       \p
       2. svid_sl(svid special action) & scos_sl(scos special action) & cvid_sl(cvid special action) & ccos_sl(ccos special action)  will be set configuration to
       svlan id, scos, cvlan id, ccos, seeing enum ctc_vlan_tag_sl_t.
       \p
       for example:
       \p
       egress vlan translation(for example:s+c->s'+c'):
       \p
           (1)stag_op:CTC_VLAN_TAG_OP_REP_OR_ADD;
           \p
           (2)ctag_op:CTC_VLAN_TAG_OP_REP_OR_ADD;
           \p
           (3)svid_sl:CTC_VLAN_TAG_SL_NEW;
           \p
           (4)cvid_sl:CTC_VLAN_TAG_SL_NEW;
           \p
       QinQ(c+s->c):
       \p
           (1)stag_op:CTC_VLAN_TAG_OP_DEL;
           \p
           (2)svid_sl:CTC_VLAN_TAG_SL_AS_RECEIVE;

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_egress_vlan_mapping(uint8 lchip, uint32 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping);

/**
 @brief The function is to remove one egress vlan mapping entry on the port in EPE

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Egress vlan mapping info, include key info to be deleted

 @remark
       Delete configuration egress vlan mapping on gport with some special vid or vrange_grpid or cos or not
       Key type(any combination) will be the egress mapping key, see key in struct ctc_egress_vlan_mapping_t.
       If not using cvid/svid/scos/ccos as key, CTC_EGRESS_VLAN_MAPPING_KEY_NONE will be set(only source port as key).
       If any entry is matched, it will do the action as action in struct ctc_egress_vlan_mapping_action_t.
       \p
       Except the key and action you must set, you must configure as follow:
       \p
       1. stag_op(stag special action) & ctag_op(ctag special action) will be set configuration to stag & ctag, seeing enum ctc_vlan_tag_op_t.
       \p
       2. svid_sl(svid special action) & scos_sl(scos special action) & cvid_sl(cvid special action) & ccos_sl(ccos special action)  will be set configuration to
       svlan id, scos, cvlan id, ccos, seeing enum ctc_vlan_tag_sl_t.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_egress_vlan_mapping(uint8 lchip, uint32 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping);

/**
 @brief  Get one egress vlan mapping entry

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_vlan_mapping  Egress vlan mapping info

 @remark
       Get one egress vlan mapping entry based on the given key.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_get_egress_vlan_mapping(uint8 lchip, uint32 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping);

/**
 @brief The function is to add egress vlan mapping default entry on the port

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @param[in] p_action        Default egress entry action

 @remark
      Configure egress vlan mapping default action on gport for packet transmitted from this gport(direction egress).
      If not matching any entry,packet will do this action just as flag in struct ctc_vlan_miss_t, only supporting two action: discard and do nothing.
      By default, if no configure default action, packet will do nothing.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_add_default_egress_vlan_mapping(uint8 lchip, uint32 gport, ctc_vlan_miss_t* p_action);

/**
 @brief The function is to remove egress vlan mapping default entry of port

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @remark
      Recover configuration of egress vlan mapping default action to do nothing.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_default_egress_vlan_mapping(uint8 lchip, uint32 gport);

/**
 @brief The function is to remove all egress vlan mapping entries on port

 @param[in] lchip    local chip id

 @param[in] gport           Global phyical port of system

 @remark
      Remove all egress vlan mapping configuration for this gport(direction egress).

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_vlan_remove_all_egress_vlan_mapping_by_port(uint8 lchip, uint32 gport);

/**
 @brief Set vlan based MAC authentication

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[in] enable      a boolean value denote vlan based MAC auth enable or not

 @remark  Set vlan mac authen enable. Used with CTC_L2_DFT_VLAN_FLAG_VLAN_MAC_AUTH.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_set_mac_auth(uint8 lchip, uint16 vlan_id, bool enable);

/**
 @brief Get vlan based MAC authentication

 @param[in] lchip    local chip id

 @param[in] vlan_id         802.1q vlan id

 @param[out] enable     a boolean value denote vlan based MAC auth enable or not

 @remark  Get vlan mac authentication. Used with CTC_L2_DFT_VLAN_FLAG_VLAN_MAC_AUTH.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_vlan_get_mac_auth(uint8 lchip, uint16 vlan_id, bool* enable);


/**@} end of @addtogroup EgressVlanEdit EgressVlanEdit*/

/**@} end of @addtogroup vlan VLAN*/

#ifdef __cplusplus
}
#endif

#endif

