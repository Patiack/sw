/**
 @file sys_greatbelt_l3if.h

 @date 2009-12-7

 @version v2.0

---file comments----
*/
#ifndef _SYS_GREATBELT_L3IF
#define _SYS_GREATBELT_L3IF
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

#define SYS_L3IF_DBG_INFO(FMT, ...)                          \
    do                                                     \
    { \
        CTC_DEBUG_OUT_INFO(l3if, l3if, L3IF_SYS, FMT, ##__VA_ARGS__); \
    } while (0)
#define SYS_L3IF_DBG_FUNC(FMT, ...)                          \
    do \
    { \
        CTC_DEBUG_OUT_FUNC(l3if, l3if, L3IF_SYS); \
    } while (0)

enum sys_l3if_aclaos_property_e
{

    SYS_L3IF_PROP_L3ACL_EN,
    SYS_L3IF_PROP_L3QOS_LKUP_EN,
    SYS_L3IF_PROP_L3ACL_ROUTEDONLY,
    SYS_L3IF_PROP_L3QOS_LABEL,
    SYS_L3IF_PROP_L3ACL_LABEL,
    SYS_L3IF_EGS_PROP_L3ACL_ROUTED_ONLY,
    SYS_L3IF_EGS_PROP_L3ACL_EN,
    SYS_L3IF_EGS_PROP_L3QOS_LOOKUP_EN,
    SYS_L3IF_EGS_PROP_L3ACL_LABEL,
    SYS_L3IF_EGS_PROP_L3QOS_LABEL,
}
;
typedef enum sys_l3if_aclaos_property_e  sys_l3if_aclaos_property_t;
enum sys_l3if_span_property_e
{

    SYS_L3IF_PROP_L3SPAN_EN,
    SYS_L3IF_PROP_L3SPAN_ID,
    SYS_L3IF_EGS_PROP_SPAN_EN,
    SYS_L3IF_EGS_PROP_SPAN_ID,

}
;
typedef enum sys_l3if_span_property_e  sys_l3if_span_property_t;

struct sys_l3if_prop_s
{
    uint16  vlan_id;
    uint16  gport;
    uint8   vaild;
    uint8   router_mac_label;
    uint8   l3if_type; /**< the type of l3interface , CTC_L3IF_TYPE_XXX */
    uint8   rmac_prefix; /**< high 40 bit mac prefiex */
    uint16  vlan_ptr; /**< Vlanptr */
    uint16  l3if_id; /**< l3if id */

};
typedef struct sys_l3if_prop_s sys_l3if_prop_t;

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
sys_greatbelt_l3if_init(uint8 lchip, ctc_l3if_global_cfg_t* l3if_global_cfg);

/**
 @brief De-Initialize l3if module
*/
extern int32
sys_greatbelt_l3if_deinit(uint8 lchip);

/**
@brief    Create  l3 interfaces
*/
extern int32
sys_greatbelt_l3if_create(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if);

/**
   @brief    Delete  l3 interfaces
*/
extern int32
sys_greatbelt_l3if_delete(uint8 lchip, uint16 l3if_id, ctc_l3if_t* l3_if);


extern int32
sys_greatbelt_l3if_get_l3if_id(uint8 lchip, ctc_l3if_t* p_l3_if, uint16* l3if_id);

/**
@brief   Judge Routed port  according to global logic port ID
*/
extern bool
sys_greatbelt_l3if_is_port_phy_if(uint8 lchip, uint16 gport);

/**
@brief   get port is subif port
*/
extern bool
sys_greatbelt_l3if_is_port_sub_if(uint8 lchip, uint16 gport);

/**
   @brief    Config  l3 interface's  properties
*/
extern int32
sys_greatbelt_l3if_set_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32 value);

/**
@brief    Get  l3 interface's properties according to interface id
*/
extern int32
sys_greatbelt_l3if_get_property(uint8 lchip, uint16 l3if_id, ctc_l3if_property_t l3if_prop, uint32* value);

/**
@brief    Set  l3 interface's properties according to interface id
*/
extern int32
sys_greatbelt_l3if_set_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool enbale);

/**
@brief   Get  l3 interface's properties according to interface id
*/
extern int32
sys_greatbelt_l3if_get_exception3_en(uint8 lchip, uint16 l3if_id, uint8 index, bool* enbale);

/**
   @brief    Config  l3 interface's  aclqos properties
*/
extern int32
sys_greatbelt_l3if_set_aclqos_property(uint8 lchip, uint16 l3if_id, sys_l3if_aclaos_property_t l3if_prop, uint32 value[CTC_MAX_LOCAL_CHIP_NUM]);

/**
@brief    Get  l3 interface's aclqos properties according to interface id
*/
extern int32
sys_greatbelt_l3if_get_aclqos_property(uint8 lchip, uint16 l3if_id, sys_l3if_aclaos_property_t l3if_prop, uint32 value[CTC_MAX_LOCAL_CHIP_NUM]);

/**
@brief   Config  l3 interface's  span properties
*/
extern int32
sys_greatbelt_l3if_set_span_property(uint8 lchip, uint16 l3if_id, sys_l3if_span_property_t l3if_prop, uint32 value);

/**
@brief   Get  l3 interface's span properties  according to interface id
*/
extern int32
sys_greatbelt_l3if_get_span_property(uint8 lchip, uint16 l3if_id, sys_l3if_span_property_t l3if_prop, uint32* value);

/**
@brief   Get  l3 interface's info,such as l3if id,vlanPtr etc
*/
extern int32
sys_greatbelt_l3if_get_l3if_info(uint8 lchip, uint8 type, sys_l3if_prop_t* l3if_prop);

/**
@brief   Get  l3 interface's info,such as l3if id,vlanPtr etc
*/
extern int32
sys_greatbelt_l3if_get_l3if_info_with_port_and_vlan(uint8 lchip, uint16 gport, uint16 vlan_id, sys_l3if_prop_t* l3if_prop);

/**********************************************************************************
                   ----  L3 interfaces's router mac    ----
***********************************************************************************/
/**
@brief    Config  40bits  virtual router mac prefix
*/
extern int32
sys_greatbelt_l3if_set_vmac_prefix(uint8 lchip, ctc_l3if_route_mac_type_t prefix_type, mac_addr_t mac_40bit);

/**
@brief    Get  40bits   virtual router mac prefix
*/
extern int32
sys_greatbelt_l3if_get_vmac_prefix(uint8 lchip, ctc_l3if_route_mac_type_t prefix_type, mac_addr_t mac_40bit);

/**
@brief    Config a low 8 bits virtual router - mac  in the L3 interface, it can config up to 4 VRIDs  for a index
*/
extern int32
sys_greatbelt_l3if_add_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac);

/**
@brief    Config a low 8 bits virtual router - mac  in the L3 interface, it can config up to 4 VRIDs  for a index
*/
extern int32
sys_greatbelt_l3if_remove_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac);

/**
@brief    Get a low 8 bits router - mac  according to interface id
*/
extern int32
sys_greatbelt_l3if_get_vmac_low_8bit(uint8 lchip, uint16 l3if_id, ctc_l3if_vmac_t* p_l3if_vmac);

/**
@brief    Config  router mac
*/
extern int32
sys_greatbelt_l3if_set_router_mac(uint8 lchip, mac_addr_t mac_addr);

/**
@brief    Get  router mac
*/
extern int32
sys_greatbelt_l3if_get_router_mac(uint8 lchip, mac_addr_t mac_addr);

/**
@brief get max vrf id , 0 means disable else vrfid must less than return value
*/
extern uint16
sys_greatbelt_l3if_get_max_vrfid(uint8 lchip, uint8 ip_ver);

extern int32
sys_greatbelt_l3if_set_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats);

extern int32
sys_greatbelt_l3if_get_vrf_stats_en(uint8 lchip, ctc_l3if_vrf_stats_t* p_vrf_stats);

#ifdef __cplusplus
}
#endif

#endif /*_SYS_HUMBER_L3IF*/

