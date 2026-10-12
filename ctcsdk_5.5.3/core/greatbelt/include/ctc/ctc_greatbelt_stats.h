/**
 @file ctc_greatbelt_stats.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-12-15

 @version v2.0

   This file contains the API designed to support implementation of stats.
   \p
   Stats will be including mac stats and flow stats.
   \p
   Mac stats will be including mac rx stats and mac tx stats. Furthermore, it can support detail stats and plus stats.
   Detail stats denotes the stats from register. Plus stats denotes the stats from sdk, the plus result of detail stats.
   \p
   Flow stats will support multiple application stats, including vlan, nexthop, police, userid, enqueue, dequeue, port
   stats etc.


*/

#ifndef _CTC_GREATBELT_STATS_H
#define _CTC_GREATBELT_STATS_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_const.h"
#include "ctc_stats.h"

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
 @addtogroup stats STATS
 @{
*/

/**
 @brief Init statistics table and register

 @param[in] lchip    local chip id

 @param[in] stats_global_cfg  Initialize Stats module

 @remark
     Initialize stats feature module.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_init(uint8 lchip, void* stats_global_cfg);

/**
 @brief De-Initialize stats module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the stats configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_stats_deinit(uint8 lchip);

/**********************************************************************************
                        Define MAC base stats functions
***********************************************************************************/

/**
 @brief Set Mac base stats property

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system

 @param[in] mac_stats_prop_type  the property type to set

 @param[in] prop_data  the property value to set

 @remark
      Set mac stats configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_set_mac_stats_cfg(uint8 lchip, uint32 gport, ctc_mac_stats_prop_type_t mac_stats_prop_type, ctc_mac_stats_property_t prop_data);

/**
 @brief Get Mac base stats property

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system

 @param[in] mac_stats_prop_type  the property type to get

 @param[out] p_prop_data  the property value to get

 @remark
      Get info about mac stats configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_get_mac_stats_cfg(uint8 lchip, uint32 gport, ctc_mac_stats_prop_type_t mac_stats_prop_type, ctc_mac_stats_property_t* p_prop_data);

/**
 @brief Get Mac base stats

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system

 @param[in] dir  MAC base stats direction

 @param[out] p_stats  MAC base stats value

 @remark
     Get mac stats info, as two mode:
     \p
     rx mode;
     \p
     tx mode;


 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_get_mac_stats(uint8 lchip, uint32 gport, ctc_mac_stats_dir_t dir, ctc_mac_stats_t* p_stats);

/**
 @brief Reset Mac base stats

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system

 @param[in] dir  MAC base stats direction

 @remark
    Reset mac stats info in register.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_clear_mac_stats(uint8 lchip, uint32 gport, ctc_mac_stats_dir_t dir);

/**
 @brief Get CPU Mac base stats

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system, low 8bit value is ignored, since cpu mac is fixed.

 @param[out] p_stats  MAC base stats value

 @remark
     Get cpu stats info.

 @return CTC_E_XXX

*/

extern int32
ctc_greatbelt_stats_get_cpu_mac_stats(uint8 lchip, uint32 gport, ctc_cpu_mac_stats_t* p_stats);

/**
 @brief Reset CPU Mac base stats

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system, low 8bit value is ignored, since cpu mac is fixed.

 @remark
    Clear cpu stats info in register.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_clear_cpu_mac_stats(uint8 lchip, uint32 gport);

/**********************************************************************************
                        Define stats general options functions
***********************************************************************************/

/**
 @brief Set port log discard stats enable

 @param[in] lchip    local chip id

 @param[in] bitmap    stats discard type

 @param[in] enable    a boolean value denote log port discard stats is enable

 @remark
     Set drop packet stats configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_set_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool enable);

/**
 @brief Get port log discard stats enable

 @param[in] lchip    local chip id

 @param[in] bitmap    stats discard type

 @param[out] enable    a boolean value denote log port discard stats is enable

 @remark
     Get drop packet stats configuration info.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_get_drop_packet_stats_en(uint8 lchip, ctc_stats_discard_t bitmap, bool* enable);

/**
 @brief Get port log stats

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system

 @param[in] dir  direction

 @param[out] p_stats    port log stats

 @remark
     Get port log stats info.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_get_port_log_stats(uint8 lchip, uint32 gport, ctc_direction_t dir, ctc_stats_basic_t* p_stats);

/**
 @brief clear port log stats

 @param[in] lchip    local chip id

 @param[in] gport  global port of the system

 @param[in] dir  direction

 @remark
     Clear port log stats info in register.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_clear_port_log_stats(uint8 lchip, uint32 gport, ctc_direction_t dir);

/**
 @brief Set stats general property

 @param[in] lchip    local chip id

 @param[in] stats_param  global forwarding stats property type

 @param[in] stats_prop   the property value to set

 @remark
     Set stats global configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_set_global_cfg(uint8 lchip, ctc_stats_property_param_t stats_param, ctc_stats_property_t stats_prop);

/**
 @brief Get stats general property

 @param[in] lchip    local chip id

 @param[in] stats_param  global forwarding stats property type

 @param[out] p_stats_prop   the property value to get

 @remark
     Set stats global configuration info.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_get_global_cfg(uint8 lchip, ctc_stats_property_param_t stats_param, ctc_stats_property_t* p_stats_prop);

/**
 @brief Create statsid

 @param[in] lchip    local chip id

 @param[in] statsid  stats property including stats type and statsid

 @remark
     Create stats id by statsid.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_create_statsid(uint8 lchip, ctc_stats_statsid_t* statsid);

/**
 @brief Destroy statsid

 @param[in] lchip    local chip id

 @param[in] stats_id  stats id

 @remark
     Destroy stats id by stats_id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_destroy_statsid(uint8 lchip, uint32 stats_id);

/**
 @brief Get stats info by statsid

 @param[in] lchip    local chip id

 @param[in] stats_id  stats id

 @param[out] p_stats  stats info

 @remark
     Get stats info by stats_id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_get_stats(uint8 lchip, uint32 stats_id, ctc_stats_basic_t* p_stats);

/**
 @brief Clear stats info by statsid

 @param[in] lchip    local chip id

 @param[in] stats_id  stats id

 @remark
     Clear stats info by stats_id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_clear_stats(uint8 lchip, uint32 stats_id);

/**
 @brief forward stats interrupt callback function

 @param[in] lchip    local chip id

 @param[in] gchip    Global chip of the system

 @remark
     Set stats interrupt callback function.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_intr_callback_func(uint8 lchip, uint8* gchip);

/**
 @brief set dma mac stats callback func

 @param[in] lchip    local chip id

 @param[in] cb    Dma mac stats callback func

 @param[in] userdata    Userdata

 @remark
     Set dma mac stats callback func.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_register_cb(uint8 lchip, ctc_stats_sync_fn_t cb, void* userdata);

/**
 @brief set stats interval time

 @param[in] lchip    local chip id

 @param[in] stats_interval    stats interval time(s)

 @remark
     Set stats interval time.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stats_set_syncup_cb_internal(uint8 lchip, uint32 stats_interval);

/**@} end of @addtogroup stats STATS  */

#ifdef __cplusplus
}
#endif

#endif

