/**
 @file sys_greatbelt_oam_debug.h

 @date 2010-1-19

 @version v2.0

  The file defines Macro, stored data structure for  Ethernet OAM module
*/
#ifndef _SYS_GREATBELT_OAM_DEBUG_H
#define _SYS_GREATBELT_OAM_DEBUG_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************************
 *
 * Header Files
 *
****************************************************************************/

#include "ctc_debug.h"

/****************************************************************************
*
* Defines and Macros
*
****************************************************************************/

#define SYS_OAM_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(oam, oam, OAM_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0);

#define SYS_OAM_DBG_FUNC()           SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__)
#define SYS_OAM_DBG_INFO(FMT, ...)  SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__)
#define SYS_OAM_DBG_ERROR(FMT, ...) SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__)
#define SYS_OAM_DBG_PARAM(FMT, ...) SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, FMT, ##__VA_ARGS__)
#define SYS_OAM_DBG_DUMP(FMT, ...) SYS_OAM_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__)
/****************************************************************************
*
* Defines and Macros
*
****************************************************************************/

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/

/****************************************************************************
 *
* Function
*
****************************************************************************/
extern int32
sys_greatbelt_oam_dump_maid(uint8 lchip, uint32 maid_index);
extern int32
sys_greatbelt_oam_dump_ma(uint8 lchip, uint32 ma_index);
extern int32
sys_greatbelt_oam_dump_lmep(uint8 lchip, uint32 lmep_index);
extern int32
sys_greatbelt_oam_dump_rmep(uint8 lchip, uint32 rmep_index);
extern int32
sys_greatbelt_oam_dump_mep(uint8 lchip, ctc_oam_key_t* p_oam_key);
extern int32
sys_greatbelt_oam_show_oam_property(uint8 lchip, ctc_oam_property_t* p_prop);
extern int32
sys_greatbelt_oam_internal_property(uint8 lchip);
extern int32
sys_greatbelt_oam_internal_maid_property(uint8 lchip, ctc_oam_maid_len_format_t maid_len);

extern int32
sys_greatbelt_oam_bfd_session_up_time(uint8 lchip);
extern int32
sys_greatbelt_oam_bfd_pf_interval(uint8 lchip, uint32 usec);

extern int32
sys_greatbelt_oam_bfd_session_state(uint8 lchip, uint32 cnt);

#ifdef __cplusplus
}
#endif

#endif

