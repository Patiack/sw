/**
 @file sys_greatbelt_vlan_mapping.h

 @date 2009-12-11

 @version v2.0


*/

#ifndef _SYS_GREATBELT_VLAN_MAPPING_H
#define _SYS_GREATBELT_VLAN_MAPPING_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_vlan.h"
/**************************************************************
*
* Macro and Defines
*
***************************************************************/

/**************************************************************
*
* Function
*
***************************************************************/
extern int32
sys_greatbelt_vlan_mapping_init(uint8 lchip);
extern int32
sys_greatbelt_vlan_mapping_deinit(uint8 lchip);

extern int32
sys_greatbelt_vlan_add_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping);

extern int32
sys_greatbelt_vlan_get_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping);

extern int32
sys_greatbelt_vlan_update_vlan_mapping_ext(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping);
extern int32
sys_greatbelt_vlan_update_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping);
extern int32
sys_greatbelt_vlan_remove_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_mapping_t* p_vlan_mapping);

extern int32
sys_greatbelt_vlan_add_default_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_miss_t* p_action);
extern int32
sys_greatbelt_vlan_remove_default_vlan_mapping(uint8 lchip, uint16 gport);
extern int32
sys_greatbelt_vlan_remove_all_vlan_mapping_by_port(uint8 lchip, uint16 gport);

extern int32
sys_greatbelt_vlan_add_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping);
extern int32
sys_greatbelt_vlan_remove_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping);
extern int32
sys_greatbelt_vlan_get_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_egress_vlan_mapping_t* p_vlan_mapping);
extern int32
sys_greatbelt_vlan_add_default_egress_vlan_mapping(uint8 lchip, uint16 gport, ctc_vlan_miss_t* p_action);
extern int32
sys_greatbelt_vlan_show_default_vlan_mapping(uint8 lchip, uint16 gport);
extern int32
sys_greatbelt_vlan_remove_default_egress_vlan_mapping(uint8 lchip, uint16 gport);
extern int32
sys_greatbelt_vlan_remove_all_egress_vlan_mapping_by_port(uint8 lchip, uint16 gport);

extern int32
sys_greatbelt_vlan_get_vlan_range_type(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool* is_svlan);
extern int32
sys_greatbelt_vlan_create_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info, bool is_svlan);
extern int32
sys_greatbelt_vlan_destroy_vlan_range(uint8 lchip, ctc_vlan_range_info_t* vrange_info);

extern int32
sys_greatbelt_vlan_add_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range);
extern int32
sys_greatbelt_vlan_remove_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_t* vlan_range);
extern int32
sys_greatbelt_vlan_get_vlan_range_member(uint8 lchip, ctc_vlan_range_info_t* vrange_info, ctc_vlan_range_group_t* vrange_group, uint8* count);

extern int32
sys_greatbelt_vlan_add_flex_vlan_mapping_entry(uint8 lchip, uint8 label_id, ctc_vlan_mapping_t* p_vlan_mapping);

extern int32
sys_greatbelt_vlan_remove_flex_vlan_mapping_entry(uint8 lchip, uint8 label_id, ctc_vlan_mapping_t* p_vlan_mapping);

extern int32
sys_greatbelt_vlan_add_flex_vlan_mapping_default_entry(uint8 lchip, uint8 label_id, ctc_vlan_miss_t* p_action);

extern int32
sys_greatbelt_vlan_remove_flex_vlan_mapping_default_entry(uint8 lchip, uint8 label_id);

#ifdef __cplusplus
}
#endif

#endif

