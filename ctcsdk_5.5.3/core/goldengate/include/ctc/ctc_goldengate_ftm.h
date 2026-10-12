/**
    @file ctc_goldengate_ftm.h

    @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

    @date 2012-10-19

    @version v2.0


  Goldengate FTM technology support flexible table sizes targeting different application scenario.
  All FTM tables are dynamic which reside in one or more physical memory block(s).
  The size of the table is decided based on a given profile and  memory is allocated at system initialization
  As a highlight of Greatbelt, majority of the FTM memory uses eDRAM technology
  achieving higher density with less power per bit.

\S ctc_ftm.h:ctc_ftm_spec_t
\S ctc_ftm.h:ctc_ftm_misc_info_t
\S ctc_ftm.h:ctc_ftm_profile_info_t
*/

#ifndef _CTC_GOLDENGATE_FTM_H
#define _CTC_GOLDENGATE_FTM_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

/**
 @addtogroup allocation FTM
 @{
*/

/**
 @brief Profile information

 @param[in] lchip    local chip id

 @param[in] ctc_profile_info  allocation profile information

 @remark profile_type was assgint to select the profile type, refer to ctc_ftm_profile_type_t

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_ftm_mem_alloc(uint8 lchip, ctc_ftm_profile_info_t* ctc_profile_info);

/**
 @brief free Profile information

 @param[in] lchip    local chip id

 @remark deinit the ftm resource

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_ftm_mem_free(uint8 lchip);

/**
 @brief Get hash polynomial

 @param[in] lchip    local chip id

 @param[in] hash_poly  hash polynomial info

 @remark Get the current hash polynomial and the hash polynomial capability of the specific memory id

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_ftm_get_hash_poly(uint8 lchip, ctc_ftm_hash_poly_t* hash_poly);

/**
 @brief Set hash polynomial

 @param[in] lchip    local chip id

 @param[in] hash_poly  hash polynomial info

 @remark Set a new hash polynomial to the specific memory id

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_ftm_set_hash_poly(uint8 lchip, ctc_ftm_hash_poly_t* hash_poly);

/**
 @brief set spec Profile

 @param[in] lchip    local chip id

 @param[in] spec_type   spec type

 @param[in] value   profile value

 @remark Set chip capability with spec_type and value

 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_ftm_set_profile_specs(uint8 lchip, uint32 spec_type, uint32 value);

/**
 @brief get spec Profile

 @param[in] lchip    local chip id

 @param[in] spec_type   spec type

 @param[out] value   got the profile value

 @remark Get capability with spec_type
 
 @return CTC_E_XXX

*/
extern int32
ctc_goldengate_ftm_get_profile_specs(uint8 lchip, uint32 spec_type, uint32* value);

/**@} end of @addtogroup allocation FTM  */

#ifdef __cplusplus
}
#endif

#endif

