/**
 @file sys_greatbelt_aps.h

 @date 2010-3-10

 @version v2.0

*/

#ifndef _SYS_GREATBELT_APS_H
#define _SYS_GREATBELT_APS_H
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

#define APS_LOCK(lchip) \
    if (p_gb_aps_master[lchip]->p_aps_mutex) sal_mutex_lock(p_gb_aps_master[lchip]->p_aps_mutex)

#define APS_UNLOCK(lchip) \
    if (p_gb_aps_master[lchip]->p_aps_mutex) sal_mutex_unlock(p_gb_aps_master[lchip]->p_aps_mutex)


enum sys_aps_flag_e
{
    SYS_APS_FLAG_PROTECT_EN                  = 0x0001,   /**< If fast_x_aps_en is set, protect_en is egnored, the bit must from hardware */
    SYS_APS_FLAG_NEXT_W_APS_EN               = 0x0002,   /**< Use next_w_aps neglect working_gport */
    SYS_APS_FLAG_NEXT_P_APS_EN               = 0x0004,   /**< Use next_w_aps neglect protection_gport */
    SYS_APS_FLAG_SHARE_NEXTHOP               = 0x0008,   /**< Indicate pw use the same nh */
    SYS_APS_FLAG_L2_APS_PHYSICAL_ISOLATED    = 0x0010,   /**< Indicate port aps use physical isolated path */
    SYS_APS_FLAG_HW_BASED_FAILOVER_EN        = 0x0020,   /**< Used for 1-level link fast aps, by hardware switch path when link down */
    SYS_APS_FLAG_RAPS                        = 0x0040,   /**< Used for raps */
    MAX_SYS_APS_FLAG
};
typedef enum sys_aps_flag_e sys_aps_flag_t;


struct sys_aps_bridge_s
{
    uint16 working_gport;
    uint16 protection_gport;

     /*  bool protect_en;                    //< If fast_x_aps_en is set, protect_en is egnored, the bit must from hardware */
     /*  uint8 aps_failover_en;                //< Used for 1-level link fast aps, by hardware switch path when link down */
     /*  bool next_w_aps_en;                 //< Use next_w_aps neglect working_gport */
     /*  bool next_p_aps_en;                 //< Use next_w_aps neglect protection_gport */
    uint16 w_l3if_id;                   /**< working l3if id */
    uint16 p_l3if_id;                   /**< protection l3if id */
    uint16 next_w_aps;                  /**< Next working aps bridge group id */
    uint16 next_p_aps;                   /**< Next protecting aps bridge group id */
     /*  bool share_nh;                         //< Indicate pw use the same nh */
     /*  bool physical_isolated;             //< Indicate port aps use physical isolated path */

     /* bool raps_en;*/
    uint32 raps_nhid;
    ctc_slist_t* local_member_list[CTC_MAX_CHIP_NUM];
    uint16 raps_group_id;
    uint16          flag;                /**< it's the sys_aps_flag_t value */
};
typedef struct sys_aps_bridge_s sys_aps_bridge_t;

struct sys_aps_master_s
{
    ctc_vector_t* aps_bridge_vector;
    sal_mutex_t* p_aps_mutex;
    ctc_slist_t* raps_grp_list;
    uint8 rsv[3];
};
typedef struct sys_aps_master_s sys_aps_master_t;

/****************************************************************
*
* Functions
*
****************************************************************/
extern int32
sys_greatbelt_aps_init(uint8 lchip);

/**
 @brief De-Initialize aps module
*/
extern int32
sys_greatbelt_aps_deinit(uint8 lchip);

extern int32
sys_greatbelt_aps_create_aps_bridge_group(uint8 lchip, uint16 aps_bridge_group_id, ctc_aps_bridge_group_t* aps_group);
extern int32
sys_greatbelt_aps_remove_aps_bridge_group(uint8 lchip, uint16 aps_bridge_group_id);

extern int32
sys_greatbelt_aps_set_aps_bridge_working_path(uint8 lchip, uint16 aps_bridge_group_id, uint16 gport);
extern int32
sys_greatbelt_aps_get_aps_bridge_working_path(uint8 lchip, uint16 aps_bridge_group_id, uint32* gport);

extern int32
sys_greatbelt_aps_set_aps_bridge_protection_path(uint8 lchip, uint16 aps_bridge_group_id, uint16 gport);
extern int32
sys_greatbelt_aps_get_aps_bridge_protection_path(uint8 lchip, uint16 aps_bridge_group_id, uint32* gport);

extern int32
sys_greatbelt_aps_set_aps_bridge(uint8 lchip, uint16 aps_bridge_group_id, bool protect_en);
extern int32
sys_greatbelt_aps_get_aps_bridge(uint8 lchip, uint16 aps_bridge_group_id, bool* protect_en);

extern int32
sys_greatbelt_aps_get_aps_raps_en(uint8 lchip, uint16 aps_bridge_group_id, bool* raps_en);

extern int32
sys_greatbelt_aps_set_aps_select(uint8 lchip, uint16 aps_select_group_id, bool protect_en);
extern int32
sys_greatbelt_aps_get_aps_select(uint8 lchip, uint16 aps_select_group_id, bool* protect_en);

extern int32
sys_greatbelt_aps_create_raps_mcast_group(uint8 lchip, uint16 group_id);
extern int32
sys_greatbelt_aps_remove_raps_mcast_group(uint8 lchip, uint16 group_id);
extern int32
sys_greatbelt_aps_update_raps_mcast_member(uint8 lchip, ctc_raps_member_t* p_raps_mem);

extern int32
sys_greatbelt_aps_update_l2edit(uint8 lchip, uint16 aps_bridge_group_id, uint32 ds_l2edit_ptr, bool is_protection);

extern int32
sys_greatbelt_aps_set_share_nh(uint8 lchip, uint16 aps_bridge_group_id, bool share_nh);

extern int32
sys_greatbelt_aps_get_ports(uint8 lchip, uint16 aps_bridge_group_id, ctc_aps_bridge_group_t* aps_group);

int32
sys_greatbelt_aps_set_l2_shift(uint8 lchip, uint16 aps_bridge_group_id, bool shift_l2);

int32
sys_greatbelt_aps_get_next_aps_working_path(uint8 lchip, uint16 aps_bridge_group_id, uint16* next_aps_id);

int32
sys_greatbelt_aps_get_next_aps_protecting_path(uint8 lchip, uint16 aps_bridge_group_id, uint16* next_aps_id);

int32
sys_greatbelt_aps_get_aps_physical_isolated(uint8 lchip, uint16 aps_bridge_group_id, uint8* physical_isolated);

int32
sys_greatbelt_aps_get_aps_bridge_current_working_port(uint8 lchip, uint16 aps_bridge_group_id, uint16* gport);

int32
sys_greatbelt_aps_get_bridge(uint8 lchip, uint16 aps_bridge_group_id, sys_aps_bridge_t* p_aps_bridge, void* ds_aps_bridge);

int32
sys_greatbelt_aps_set_aps_bridge_group(uint8 lchip, uint16 aps_bridge_group_id, ctc_aps_bridge_group_t* aps_group);

#ifdef __cplusplus
}
#endif

#endif

