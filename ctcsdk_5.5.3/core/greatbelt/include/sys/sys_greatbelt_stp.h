/**
 @file

 @date 2009-10-20

 @version v2.0

*/

#ifndef _SYS_GREATBELT_STP_H
#define _SYS_GREATBELT_STP_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_const.h"

#include "ctc_l2.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define SYS_STP_STATE_ENTRY_NUM_PER_PORT (CTC_STP_NUM / 16)   /* entry num of one port */
#define SYS_STP_STATE_SHIFT 3

#define SYS_STP_ID_VALID_CHECK(stp_id)                    \
    do {                                                  \
        if ((stp_id) > CTC_MAX_STP_ID){ \
            return CTC_E_STP_INVALID_STP_ID;                    \
        }                                               \
    } while (0)

#define SYS_STP_DBG_INFO(FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT_INFO(l2, stp, L2_STP_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_STP_DBG_FUNC()                          \
    do \
    { \
        CTC_DEBUG_OUT_FUNC(l2, stp, L2_STP_SYS); \
    } while (0)

/****************************************************************************
*
* Function
*
*****************************************************************************/

/**
 @brief the function is to clear all instances blonged to one port

*/
extern int32
sys_greatbelt_stp_clear_all_inst_state(uint8 lchip, uint16 gport);

/**
 @brief the function is to stp id (MSTI instance) for vlan
*/
extern int32
sys_greatbelt_stp_set_vlan_stpid(uint8 lchip, uint16 vlan_id, uint8 stpid);

/**
 @brief the function is to stp id (MSTI instance) for vlan
*/
extern int32
sys_greatbelt_stp_get_vlan_stpid(uint8 lchip, uint16 vlan_id, uint8* stpid);

/**
 @brief the function is to update the state
*/
extern int32
sys_greatbelt_stp_set_state(uint8 lchip, uint16 gport, uint8 stpid, uint8 state);

/**
 @brief the function is to get the instance of the port
*/
extern int32
sys_greatbelt_stp_get_state(uint8 lchip, uint16 gport, uint8 stpid, uint8* state);

/**
 @brief the function is to init all port stp state
*/
extern int32
sys_greatbelt_stp_init(uint8 lchip);

/**
 @brief De-Initialize stp module
*/
extern int32
sys_greatbelt_stp_deinit(uint8 lchip);

#ifdef __cplusplus
}
#endif

#endif

