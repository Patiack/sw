/**
 @file sys_goldengate_aps.h

 @date 2010-3-10

 @version v2.0

*/

#ifndef _SYS_GOLDENGATE_APS_H
#define _SYS_GOLDENGATE_APS_H
#ifdef __cplusplus
extern "C" {
#endif
/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_aps.h"
#include "ctc_const.h"
#include "ctc_vector.h"
#include "ctc_linklist.h"
#include "ctc_debug.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define CTC_APS_GROUP_BLOCK 16

#define SYS_APS_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(aps, aps, APS_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_APS_DUMP(FMT, ...)                    \
    do                                                       \
    {                                                        \
        CTC_DEBUG_OUT(aps, aps, APS_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_APS_BRIDGE_DUMP(group_id, p_bridge_info) \
    do { \
        SYS_APS_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "p_bridge_info :: \n" \
                        "     aps_bridge_id = %d, protect_en = %d, physical_isolated=%d,\n" \
                        "     working_gport =%d, protection_gport =%d, w_l3if_id=%d, p_l3if_id=%d,\n" \
                        "     next_w_aps_en=%d, next_p_aps_en=%d, next_w_aps=%d, next_p_aps=%d, \n" \
                        "     raps_en=%d, raps_group_id=%d\n", \
                        group_id, p_bridge_info->protect_en, p_bridge_info->physical_isolated, \
                        p_bridge_info->working_gport, p_bridge_info->protection_gport, p_bridge_info->w_l3if_id, p_bridge_info->p_l3if_id, \
                        p_bridge_info->next_w_aps_en, p_bridge_info->next_p_aps_en, \
                        p_bridge_info->next_aps.w_group_id, p_bridge_info->next_aps.p_group_id, \
                        p_bridge_info->raps_en, p_bridge_info->raps_group_id); \
    } while (0)

#define SYS_APS_GROUP_ID_CHECK(aps_group_id)  \
    { \
        if (aps_group_id >= TABLE_MAX_INDEX(DsApsBridge_t)){ \
            return CTC_E_APS_INVALID_GROUP_ID; } \
    }

#ifndef PACKET_TX_USE_SPINLOCK
#define APS_LOCK(lchip) \
    do { \
        if (p_gg_aps_master[lchip]->p_aps_mutex){ \
            sal_mutex_lock(p_gg_aps_master[lchip]->p_aps_mutex); } \
    } while (0)

#define APS_UNLOCK(lchip) \
   do { \
        if (p_gg_aps_master[lchip]->p_aps_mutex){ \
            sal_mutex_unlock(p_gg_aps_master[lchip]->p_aps_mutex); } \
    } while (0)
#else
#define APS_LOCK(lchip) \
    do { \
        if (p_gg_aps_master[lchip]->p_aps_mutex){ \
            sal_spinlock_lock((sal_spinlock_t*)p_gg_aps_master[lchip]->p_aps_mutex); } \
    } while (0)

#define APS_UNLOCK(lchip) \
   do { \
        if (p_gg_aps_master[lchip]->p_aps_mutex){ \
            sal_spinlock_unlock((sal_spinlock_t*)p_gg_aps_master[lchip]->p_aps_mutex); } \
    } while (0)
#endif


/****************************************************************
*
* Functions
*
****************************************************************/
extern int32
sys_goldengate_aps_init(uint8 lchip);

/**
 @brief De-Initialize aps module
*/
extern int32
sys_goldengate_aps_deinit(uint8 lchip);

extern int32
sys_goldengate_aps_create_bridge_group(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group);
extern int32
sys_goldengate_aps_update_bridge_group(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group);
extern int32
sys_goldengate_aps_remove_bridge_group(uint8 lchip, uint16 group_id);

extern int32
sys_goldengate_aps_set_bridge_path(uint8 lchip, uint16 group_id, uint16 gport, bool is_working);

extern int32
sys_goldengate_aps_get_bridge_path(uint8 lchip, uint16 group_id, uint32* gport, bool is_working);

extern int32
sys_goldengate_aps_set_protection(uint8 lchip, uint16 group_id, bool protect_en);
extern int32
sys_goldengate_aps_get_protection(uint8 lchip, uint16 group_id, bool* protect_en);

extern int32
sys_goldengate_aps_create_raps_mcast_group(uint8 lchip, uint16 group_id);
extern int32
sys_goldengate_aps_remove_raps_mcast_group(uint8 lchip, uint16 group_id);
extern int32
sys_goldengate_aps_update_raps_mcast_member(uint8 lchip, ctc_raps_member_t* p_raps_mem);

extern int32
sys_goldengate_aps_update_tunnel(uint8 lchip, uint16 group_id, uint32 ds_edit_ptr, bool is_protection);

extern int32
sys_goldengate_aps_set_share_nh(uint8 lchip, uint16 group_id, bool share_nh);

extern int32
sys_goldengate_aps_get_share_nh(uint8 lchip, uint16 group_id, bool* share_nh);

extern int32
sys_goldengate_aps_get_ports(uint8 lchip, uint16 group_id, ctc_aps_bridge_group_t* aps_group);

extern int32
sys_goldengate_aps_set_spme_en(uint8 lchip, uint16 group_id, bool sone_en);

extern int32
sys_goldengate_aps_get_next_group(uint8 lchip, uint16 group_id, uint16* next_group_id, bool is_working);

extern int32
sys_goldengate_aps_get_current_working_port(uint8 lchip, uint16 group_id, uint16* gport);


#ifdef __cplusplus
}
#endif

#endif

