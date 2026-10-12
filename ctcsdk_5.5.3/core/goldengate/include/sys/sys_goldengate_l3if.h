/**
 @file sys_goldengate_l3if.h

 @date 2009-12-7

 @version v2.0

---file comments----
*/
#ifndef _SYS_GOLDENGATE_L3IF
#define _SYS_GOLDENGATE_L3IF
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
                          Defines and Macros
***********************************************************************************/
#define SYS_L3IF_INVALID_L3IF_ID   0
#define SYS_L3IF_RSV_L3IF_ID_FOR_INTERNAL_PORT  1023       /**<  Reserve l3if id  for iloop port */

struct sys_l3if_prop_s
{
    uint16  vlan_id;
    uint16  gport;

    uint8   vaild;
    uint8   l3if_type; /**< the type of l3interface , CTC_L3IF_TYPE_XXX */
    uint8   rtmac_index;/*0~127, total router mac num*/
    uint8   rtmac_bmp; /* routerMacSelBitmap in DsRouterMac*/

    mac_addr_t egress_router_mac;/* for egress route mac*/

    uint16  vlan_ptr; /**< Vlanptr */
    uint16  l3if_id; /**< l3if id */
};
typedef struct sys_l3if_prop_s sys_l3if_prop_t;

struct sys_l3if_ecmp_if_s
{
    uint16 hw_group_id;
    uint8  intf_count;
    uint8  failover_en;
    uint16 intf_array[SYS_GG_MAX_ECPN];
    uint32 dsfwd_offset[SYS_GG_MAX_ECPN];
    uint16 ecmp_group_type;     /* refer to ctc_nh_ecmp_type_t */
    uint16 ecmp_member_base;
    uint32 stats_id;
};
typedef struct sys_l3if_ecmp_if_s sys_l3if_ecmp_if_t;

/**********************************************************************************
                      Define API function interfaces
***********************************************************************************/

/**********************************************************************************
                    ----  L3 interfaces's  properties  ----
***********************************************************************************/
/**
 @brief    init l3 interface module
*/
extern int32
sys_goldengate_l3if_init(uint8 lchip, ctc_l3if_global_cfg_t* l3if_global_cfg);

/**
 @brief De-Initialize l3if module
*/
extern int32
sys_goldengate_l3if_deinit(uint8 lchip);

/**
@brief    Create  l3 interfaces
*/
extern int32
sys_goldengate_l3if_create(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if);

/**
   @brief    Delete  l3 interfaces
*/
extern int32
sys_goldengate_l3if_delete(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if);


extern int32
sys_goldengate_l3if_get_l3if_id(uint8 lchip, ctc_l3if_t* p_l3_if, uint16* l3if_id);

/**
@brief   Judge Routed port  according to global logic port ID
*/
extern bool
sys_goldengate_l3if_is_port_phy_if(uint8 lchip, uint16 gport);

/**
@brief   get port is subif port
*/
extern bool
sys_goldengate_l3if_is_port_sub_if(uint8 lchip, uint16 gport);

/**
   @brief    Config  l3 interface's  properties
*/
extern int32
sys_goldengate_l3if_set_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value);

/**
@brief    Get  l3 interface's properties according to interface id
*/
extern int32
sys_goldengate_l3if_get_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32* value);

/**
@brief    Set  l3 interface's properties according to interface id
*/
extern int32
sys_goldengate_l3if_set_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool enbale);

/**
@brief   Get  l3 interface's properties according to interface id
*/
extern int32
sys_goldengate_l3if_get_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool* enbale);

/**
@brief   Get  l3 interface's info,such as l3if id,vlanPtr etc
*/
extern int32
sys_goldengate_l3if_get_l3if_info(uint8 lchip, uint8 type, sys_l3if_prop_t* l3if_prop);

/**
@brief   Get  l3 interface's info,such as l3if id,vlanPtr etc
*/
extern int32
sys_goldengate_l3if_get_l3if_info_with_port_and_vlan(uint8 lchip, uint16 gport, uint16 vlan_id, sys_l3if_prop_t* l3if_prop);

/**********************************************************************************
                   ----  L3 interfaces's router mac    ----
***********************************************************************************/
/**
@brief    Config  router mac
*/
extern int32
sys_goldengate_l3if_set_router_mac(uint8 lchip, mac_addr_t mac_addr);

/**
@brief    Get  router mac
*/
extern int32
sys_goldengate_l3if_get_router_mac(uint8 lchip, mac_addr_t mac_addr);

/**
     @brief      Config l3 interface router mac
*/
extern int32
sys_goldengate_l3if_set_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t router_mac);

/**
     @brief       Get l3 interface router mac
*/
extern int32
sys_goldengate_l3if_get_interface_router_mac(uint8 lchip, uint16 l3if_id, ctc_l3if_router_mac_t* router_mac);

/**
@brief get max vrf id , 0 means disable else vrfid must less than return value
*/
extern uint16
sys_goldengate_l3if_get_max_vrfid(uint8 lchip, uint8 ip_ver);

extern int32
sys_goldengate_l3if_set_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats);

extern int32
sys_goldengate_l3if_create_ecmp_if(uint8 lchip, ctc_l3if_ecmp_if_t* p_ecmp_if);

extern int32
sys_goldengate_l3if_destory_ecmp_if(uint8 lchip, uint16 ecmp_if_id);

extern int32
sys_goldengate_l3if_add_ecmp_if_member(uint8 lchip, uint16 ecmp_if_id, uint16 l3if_id);

extern int32
sys_goldengate_l3if_remove_ecmp_if_member(uint8 lchip, uint16 ecmp_if_id, uint16 l3if_id);

extern int32
sys_goldengate_l3if_get_ecmp_if_info(uint8 lchip, uint16 ecmp_if_id, sys_l3if_ecmp_if_t** p_ecmp_if);

extern uint32
sys_goldengate_l3if_binding_inner_route_mac(uint8 lchip, mac_addr_t* p_mac_addr, uint8* route_mac_index);

extern int32
sys_goldengate_l3if_unbinding_inner_route_mac(uint8 lchip, uint8 route_mac_index);
extern int32
sys_goldengate_l3if_set_lm_en(uint8 lchip, uint16 l3if_id, uint8 enable);
#ifdef __cplusplus
}
#endif

#endif /*_SYS_HUMBER_L3IF*/

