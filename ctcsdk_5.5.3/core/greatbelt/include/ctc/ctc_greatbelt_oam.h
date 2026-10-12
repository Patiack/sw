/**
 @file ctc_greatbelt_oam.h

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2010-1-19

 @version v2.0

 The Greatbelt OAM Engine provides control of the following OAM features:
 \p

 \d   The transmission of outgoing continuity check messages (CCMs)
 \d   The reception and processing of incoming CCMs
 \d   The transmission of loopback responses (LBRs) in response to loopback messages (LBMs)
 \d   The forwarding link trace messages (LTM) and link trace responses (LTR) to the CPU
 \d   The transmission of loss measurement responses (LMRs) in response to loss measurement messages (LMMs)
 \d   The time stamping and forwarding of delay measurement (DM) packets to the CPU
 \d   The transmission of outgoing client signal fail (CSF)
 \d   The reception and processing of incoming CSF.

 \p
 This module contains  OAM (Ethernet OAM(1ag/Y.1731)/MPLS-TP(Y.1731/BFD) OAM) associated APIs, apply APIs can initialize,
  add/remove maid/local MEP/remote MEP/MIP etc. All API use MEP type(ctc_oam_mep_type_t) to indicate which OAM type to support.

 \p
 Callback mechanisms are also provided for notifying the application of OAM-related events, such as the setting
 and clearing of defect.

 \p
 The module provides APIs to support OAM:
 \p
 \d Add/remove maid for OAM by ctc_oam_add_maid()/ctc_oam_remove_maid() .
 \d Add/remove/update local mep by ctc_oam_add_lmep()/ctc_oam_remove_lmep()/ctc_oam_update_lmep.
 \d Add/remove/update remote mep by ctc_oam_add_rmep()/ctc_oam_remove_rmep()/ctc_oam_update_rmep.
 \d Add/remove mip by ctc_oam_add_mip()/ctc_oam_remove_mip().
 \d Config global property for OAM by ctc_oam_set_property().
 \d Get defect MEP by ctc_oam_get_defect_info().
 \d Get mep information by ctc_oam_get_mep_info().
*/

#ifndef _CTC_GREATBELT_OAM_H
#define _CTC_GREATBELT_OAM_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 *
 * Header Files
 *
****************************************************************************/
#include "ctc_oam.h"
#include "ctc_parser.h"

/****************************************************************************
*
* Defines and Macros
*
****************************************************************************/

/****************************************************************************
 *
* Function
*
****************************************************************************/
/**
 @addtogroup oam OAM
 @{
 */

/**
 @brief   Add ma id for oam

 @param[in] lchip    local chip id

 @param[in]  p_maid    maid data structure pointer

 @remark  Add ma id , refer to ctc_oam_maid_t.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_add_maid(uint8 lchip, ctc_oam_maid_t* p_maid);

/**
 @brief   Remove ma id for oam

 @param[in] lchip    local chip id

 @param[in]  p_maid   maid data sturcture pointer

 @remark  Remove ma id , refer to ctc_oam_maid_t.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_remove_maid(uint8 lchip, ctc_oam_maid_t* p_maid);

/**
 @brief   Add local mep

 @param[in] lchip    local chip id

 @param[in]  p_lmep   local mep data sturcture pointer

 @remark  Add local mep, refer to ctc_oam_lmep_t.
          Use flag can set MEP to P2MP mode, up/down mep.
          For MEP on linkagg port cross chip, should use local MEP index allocate by system,
          and should set master_gchip to indicate which chip OAM engine to MEP resident.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_add_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep);

/**
 @brief   Remove local mep

 @param[in] lchip    local chip id

 @param[in]  p_lmep   local mep data sturcture pointer

 @remark  Remove local mep, refer to ctc_oam_lmep_t.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_remove_lmep(uint8 lchip, ctc_oam_lmep_t* p_lmep);

/**
 @brief   Update local mep

 @param[in] lchip    local chip id

 @param[in]  p_lmep   local mep data sturcture pointer

 @remark  Update local mep config , refer to ctc_oam_update_t.
          for local MEP, should set is_local to 1.
          For example, If MEP type is CTC_OAM_MEP_TYPE_ETH_Y1731,
          refer ctc_eth_oam_lmep_update_type_t can update local mep config.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_update_lmep(uint8 lchip, ctc_oam_update_t* p_lmep);

/**
 @brief   Add remote mep

 @param[in] lchip    local chip id

 @param[in]  p_rmep   remote mep data sturcture pointer

 @remark  Add remote mep, refer to ctc_oam_rmep_t.
          For P2MP mode, use different remote mep id to add remote mep,
          but should set P2MP when Add local MEP.
          For MEP on linkagg port cross chip, should use remote MEP index allocate by system.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_add_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep);

/**
 @brief   Remove remote mep

 @param[in] lchip    local chip id

 @param[in]  p_rmep   remote mep data sturcture pointer

 @remark  Remove remote mep, refer to ctc_oam_rmep_t.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_remove_rmep(uint8 lchip, ctc_oam_rmep_t* p_rmep);

/**
 @brief   Update remote mep

 @param[in] lchip    local chip id

 @param[in]  p_rmep   remote mep data sturcture pointer

 @remark  Update remote mep config , refer to ctc_oam_update_t.
          For remote MEP, should set is_local to 0 and rmep_id indicate which remote mep to update.
          For example, If MEP type is CTC_OAM_MEP_TYPE_ETH_Y1731,
          refer ctc_eth_oam_rmep_update_type_t can update remote mep config.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_update_rmep(uint8 lchip, ctc_oam_update_t* p_rmep);

/**
 @brief   Add mip

 @param[in] lchip    local chip id

 @param[in]  p_mip   mip data sturcture pointer

 @remark  Add mip, refer to ctc_oam_mip_t.
          For MIP on linkagg port cross chip, should set master_gchip
          indicated which chip to process OAM PDU.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_add_mip(uint8 lchip, ctc_oam_mip_t* p_mip);

/**
 @brief   Remove mip

 @param[in] lchip    local chip id

 @param[in]  p_mip   mip data sturcture pointer

 @remark  Remove mip, refer to ctc_oam_mip_t.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_remove_mip(uint8 lchip, ctc_oam_mip_t* p_mip);

/**
 @brief   Set oam property of oam enable and others

 @param[in] lchip    local chip id

 @param[in] p_prop property sturcture pointer

 @remark  Use MEP type to set Common OAM/ETH-OAM/TP-OAM global property, refer to ctc_oam_defect_info_t.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_set_property(uint8 lchip, ctc_oam_property_t* p_prop);

/**
 @brief   Read Oam defect cache which keep up defect info

 @param[in] lchip_id    local chip id

 @param[out] p_defect_info    point to defect cache info

 @remark  p_defect_info refer to ctc_oam_defect_info_t.
          Get defect MEP's index from error cache.
          How to use p_defect_info to get MEP index :
\p
          for(mep_bit_index = 0; mep_bit_index < 32 , mep_bit_index++)
\p
          {
\p
            if(CTC_IS_BIT_SET(mep_index_bitmap, mep_bit_index))
\p
            {
\p
                mep_index = mep_index_base*32 + mep_bit_index;
\p
            }
\p
          }
\p
 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_oam_get_defect_info(uint8 lchip_id, void* p_defect_info);

/**
 @brief   Get MEP Information from both SDK database and ASIC dataset

 @param[in] lchip_id    local chip id

 @param[in,out] p_mep_info    MEP info pointer

 @remark  Input MEP index get by ctc_oam_get_defect_info().
          According MEP index, can get MEP's defect information. Such as Loc, Mismerge,
          UnexpectedPeriod, UnexpectedMEP, UnexpectedMEGlevel.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_oam_get_mep_info(uint8 lchip_id, ctc_oam_mep_info_t* p_mep_info);



/**
 @brief   Get MEP Information from both SDK database and ASIC dataset according OAM key

 @param[in] lchip    local chip id

 @param[in,out] p_mep_info    MEP info pointer

 @remark  Input OAM key information.
          According OAM Key, can get MEP's defect information. Such as Loc, Mismerge,
          UnexpectedPeriod, UnexpectedMEP, UnexpectedMEGlevel, dMisc, BFD interval, BFD discr.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_oam_get_mep_info_with_key(uint8 lchip, ctc_oam_mep_info_with_key_t* p_mep_info);

/**
 @brief   Get OAM MEP Stats from ASIC dataset

 @param[in] lchip    local chip id

 @param[in,out] p_stat_info    MEP stats information pointer

 @remark  Input key for Local MEP, can get MEP's LM counter for dual-ended LM, LM type and LM cos type.
          For single-ended LM , not need to get LM counter use this API.

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_oam_get_stats(uint8 lchip, ctc_oam_stats_info_t* p_stat_info);

/**
 @brief  Set throughput session parameter

 @param[in] lchip    local chip id

 @param[in] p_throughput config for throughput

 @remark  set throughput global config
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_set_trpt_cfg(uint8 lchip, ctc_oam_trpt_t* p_throughput);

/**
 @brief   Set throughput session enable

 @param[in] lchip    local chip id

 @param[in] gchip global chip
 @param[in] session_id session if for throughput

 @param[in] enable enable throughput or disable

 @remark  Must set throughput parameter by ctc_greatbelt_oam_set_trpt_para first, then enable throughput session.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_set_trpt_en(uint8 lchip, uint8 gchip, uint8 session_id, uint8 enable);

/**
 @brief  Get throughput stats

 @param[in] lchip    local chip id

 @param[in] gchip global chip

 @param[in] session_id session id if for throughput

 @param[out] p_trpt_stats the statistics for throughput

 @remark  get throughput statistics
 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_get_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id,  ctc_oam_trpt_stats_t* p_trpt_stats);

/**
 @brief  Clear throughput stats

 @param[in] lchip    local chip id

 @param[in] gchip global chip

 @param[in] session_id session id if for throughput

 @remark  clear throughput statistics

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_clear_trpt_stats(uint8 lchip, uint8 gchip, uint8 session_id);

/**
 @brief   Initialization for oam

 @param[in] lchip    local chip id

 @param[in] p_cfg global configuration for oam pointer

 @remark  When p_cfg is NULL,OAM will use default global config to initialize sdk.
          \p
          By default: Enable MEP index allocated by SDK.

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_oam_init(uint8 lchip, ctc_oam_global_t* p_cfg);

/**
 @brief De-Initialize oam module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the oam configuration

 @return CTC_E_XXX
*/
extern int32
ctc_greatbelt_oam_deinit(uint8 lchip);

/**@} end of @addtogroup oam OAM*/
#ifdef __cplusplus
}
#endif

#endif

