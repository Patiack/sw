/**
 @file ctc_greatbelt_stp.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-10-16

 @version v2.0

 This module contains all the STP APIs
*/

#ifndef _CTC_GREATBELT_STP_H
#define _CTC_GREATBELT_STP_H
#ifdef __cplusplus
extern "C" {
#endif

/**********************************************************************************
              Define API function interfaces
***********************************************************************************/

/**
 @addtogroup stp STP
 @{
*/

/**
 @brief the function is to clear all instances belong to one port

 @param[in] lchip    local chip id

 @param[in] gport  global port

 @remark Destroy all STP instances and initialize the STP module to its initial configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stp_clear_all_inst_state(uint8 lchip, uint32 gport);

/**
 @brief the function is to set stp id (MSTI instance) for vlan

 @param[in] lchip    local chip id

 @param[in] vlan_id      802.1q vlan id

 @param[in] stpid        a value denoted the index of the stp instance, its value is between 0 and 0x7F.

 @remark Set the STP instance id to the VLAN specified by vlan id.
         The current stp id overwrite the previous vlan's stp id value after configuration implicitly.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stp_set_vlan_stpid(uint8 lchip, uint16 vlan_id, uint8 stpid);

/**
 @brief The function is to get stp id

 @param[in] lchip    local chip id

 @param[in] vlan_id 802.1q vlan id

 @param[out] stpid a value denoted the index of the stp instance, its value is between 0 and 0x7F.

 @remark Retrieve the stp id of the VLAN specified by vlan id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stp_get_vlan_stpid(uint8 lchip, uint16 vlan_id, uint8* stpid);

/**
 @brief the function is to update the state

 @param[in] lchip    local chip id

 @param[in] gport global port

 @param[in] stpid a value denoted the index of the stp instance, its value is between 0 and 0x7F.

 @param[in] state a value denoted the instance

 @remark Set stp state to the instance of STP state indicates by both global port and stp id.
         The stp state should use the value 0:forward, 1:blocking, and 2:learning.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stp_set_state(uint8 lchip, uint32 gport, uint8 stpid, uint8 state);

/**
 @brief the function is to get the instance of the port

 @param[in] lchip    local chip id

 @param[in] gport  global port

 @param[in] stpid a value denoted the index of the stp instance, its value is between 0 and 0x7F.

 @param[out] state  the pointer point to the getted state, will be NULL if none

 @remark Retrieve the STP state indicated by both global port and stp id.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stp_get_state(uint8 lchip, uint32 gport, uint8 stpid, uint8* state);

/**
 @brief Init stp module

 @param[in] lchip    local chip id

 @param[in] stp_global_cfg  stp global configuration

 @remark Initialize the STP module to its initial configuration.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_stp_init(uint8 lchip, void* stp_global_cfg);

/**
 @brief De-Initialize stp module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the stp configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_stp_deinit(uint8 lchip);

/**@} end of @addtogroup stp STP*/

#ifdef __cplusplus
}
#endif

#endif

