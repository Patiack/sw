/**
    @file ctc_greatbelt_ftm.h

    @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

    @date 2012-10-19

    @version v2.0


  Greatbelt FTM technology support flexible table sizes targeting different application scenario.
  All FTM tables are dynamic which reside in one or more physical memory block(s).
  The size of the table is decided based on a given profile and  memory is allocated at system initialization
  As a highlight of Greatbelt, majority of the FTM memory uses eDRAM technology
  achieving higher density with less power per bit.
\p
\p Greatbelt FTM engine incorporates the following physical memories:
\p
\d eDRAM: 20Mbits of eDRAM, organized as 8 independent accessible blocks.
      All eDRAMs are ECC protected. Effective storage excluding ECC are 224Kx80bits
\d eTCAM: 640Kbits of eTCAM organized as 8Kx80bits, support error protection
\d eSRAM: 640Kbits of eSRAM, used as for storage space for eTCAM lookup associated data

\p
\p The following profile is Greatbelt typical specification, If more profiles or modification to the profiles are necessary,
\p please contact your FAE:fae@centecnetworks.com or sales@centecnetworks.com

\p
\p
\p
\t |               |5160 | 5160 |5162/5163|5162/5163| 512X |316X |
\t |Profile        |L2VPN| L3VPN|Metro| Enterprise|Small PTN  | L2+ |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |MAC            | 64K | 8K   | 32K | 32K       | 64        | 32K |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |IP Host(v4/v6) | 2K  | 8K   | 8K  | 24K       | 2K        | 8K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |IP LPM(v4/v6)  | 4K  | 32K  | 16K | 8K        | 4K        | 4K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |L2 MC Group    | 1k  | 1K   | 1k  | 1K        | 1k        | 1k  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |L3 MC Group    | 1k  | 1K   | 1k  | 1K        | 1k        | 1k  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |Hash Flow      | 0   | 0    | 0   | 0         | 0         | 8K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |VlanXlate      |     |      |     |           |           |     |
\t |(ingr/egr)     | 4K  | 4K   | 4K  | 4K        | 2K        | 4K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |Acl(VFP/IFP/EFP)| 3K | 3K   || 3K | 3K        | 3K        | 3K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |MPLS           | 8K  | 8K   | 4K  | 0         | 4K        | 0   |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |Policer        | 8K  | 8K   | 4K  | 4K        | 4K        | 4K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |Stats(ingr/egr)| 16K | 16K  | 16K | 16K       | 8K        | 8K  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |MEP(Local/Remote)| 2K | 2K  |)| 2K | 0        | 2K        | 0   |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |APS Group      | 1K  | 1K   | 0   | 0         | 1K        | 0   |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |VRF            | 256 | 256  | 256 | 256       | 256       | 0   |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |VSI            | 1K  | 1K   | 1K  | 0         | 1K        | 0   |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |VP             | 8K  | 8K   | 8K  | 0         | 4K        | 0   |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |LAG Group      | 64  | 64   | 64  | 64        | 64        | 64  |
\t |---------------|-----|------|-----|-----------|-----------|-----|
\t |LAG Member     | 1024| 1024 | 1024| 1024      | 1024      | 1024|
\t |---------------|-----|------|-----|-----------|-----------|-----|

*/

#ifndef _CTC_GREATBELT_FTM_H
#define _CTC_GREATBELT_FTM_H
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
ctc_greatbelt_ftm_mem_alloc(uint8 lchip, ctc_ftm_profile_info_t* ctc_profile_info);

/**
 @brief Deinit Profile information

 @param[in] lchip    local chip id

 @remark deinit the ftm resource

 @return CTC_E_XXX

*/
extern int32
ctc_greatbelt_ftm_mem_free(uint8 lchip);

/**@} end of @addtogroup allocation FTM  */

#ifdef __cplusplus
}
#endif

#endif

