/**
 @file sys_greatbelt_linkagg.h

 @date 2012-3-14

 @version v2.0

 The file define APIs and types use in sys layer
*/
#ifndef _SYS_GREATBELT_STK_H
#define _SYS_GREATBELT_STK_H
#ifdef __cplusplus
extern "C" {
#endif
/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_const.h"

#include "ctc_stacking.h"
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

extern int32
sys_greatbelt_stacking_get_rsv_trunk_number(uint8 lchip, uint8* number);

extern int32
sys_greatbelt_stacking_create_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_destroy_trunk(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_add_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_remove_trunk_port(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_replace_trunk_ports(uint8 lchip, uint8 trunk_id, uint32* gports, uint8 mem_ports);

extern int32
sys_greatbelt_stacking_get_member_ports(uint8 lchip, uint8 trunk_id, uint32* p_gports, uint8* cnt);

extern int32
sys_greatbelt_stacking_add_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_remove_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_get_trunk_rchip(uint8 lchip, ctc_stacking_trunk_t* p_trunk);

extern int32
sys_greatbelt_stacking_set_property(uint8 lchip, ctc_stacking_property_t* p_prop);

extern int32
sys_greatbelt_stacking_get_property(uint8 lchip, ctc_stacking_property_t* p_prop);

extern int32
sys_greatbelt_stacking_get_enable(uint8 lchip, bool* enable, uint32* stacking_mcast_offset);

extern int32
sys_greatbelt_stacking_create_keeplive_group(uint8 lchip, uint16 group_id);

extern int32
sys_greatbelt_stacking_destroy_keeplive_group(uint8 lchip, uint16 group_id);

extern int32
sys_greatbelt_stacking_add_keeplive_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive_grp);

extern int32
sys_greatbelt_stacking_remove_keeplive_member(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive_grp);

extern int32
sys_greatbelt_stacking_get_keeplive_members(uint8 lchip, ctc_stacking_keeplive_t* p_keeplive);

extern int32
sys_greatbelt_stacking_set_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile);
extern int32
sys_greatbelt_stacking_get_trunk_mcast_profile(uint8 lchip, ctc_stacking_trunk_mcast_profile_t* p_mcast_profile);

extern int32
sys_greatbelt_stacking_get_stkhdr_len(uint8 lchip, uint16 gport, uint16 gchip, uint16* p_stkhdr_len);

extern int32
sys_greatbelt_stacking_init(uint8 lchip, void* p_cfg);

/**
 @brief De-Initialize stacking module
*/
extern int32
sys_greatbelt_stacking_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

