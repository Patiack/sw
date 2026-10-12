/**
 @file sys_greatbelt_learning_aging.h

 @date 2010-3-16

 @version v2.0

---file comments----
*/

#ifndef _SYS_GREATBELT_LEARNING_AGING_H
#define _SYS_GREATBELT_LEARNING_AGING_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_learning_aging.h"
#include "ctc_interrupt.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
enum sys_aging_domain_type_e
{
    SYS_AGING_TCAM_256,
    SYS_AGING_TCAM_8k,
    SYS_AGING_USERID_HASH,
    SYS_AGING_LPM_HASH,
    SYS_AGING_MAC_HASH
};
typedef enum sys_aging_domain_type_e sys_aging_domain_type_t;

#define  SYS_AGING_TIMER_INDEX_MAC      0
#define  SYS_AGING_TIMER_INDEX_SERVICE  1

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

/***************************************
 Learning module's sys interfaces
*****************************************/
/**
 @brief set learning action and cache threshold
*/
int32
sys_greatbelt_learning_set_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action);

/**
 @brief get learning action and cache threshold
*/
int32
sys_greatbelt_learning_get_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action);

/**
 @brief get learning cache entry valid status bitmap
*/
int32
sys_greatbelt_learning_get_cache_entry_valid_bitmap(uint8 lchip, uint16* entry_vld_bitmap);

/**
 @brief read learning cache entry
*/
int32
sys_greatbelt_learning_read_learning_cache(uint8 lchip, uint16 entry_vld_bitmap, ctc_learning_cache_t* l2_lc);

/**
 @brief Clear learning cache entry
*/
int32
sys_greatbelt_learning_clear_learning_cache(uint8 lchip, uint16 entry_vld_bitmap);

/***************************************
 Aging module's sys interfaces
*****************************************/
/**
 @brief This function is to set the aging properties
*/
extern int32
sys_greatbelt_aging_set_property(uint8 lchip, ctc_aging_table_type_t tbl_type, ctc_aging_prop_t aging_prop, uint32 value);

/**
 @brief This function is to get the aging properties
*/
extern int32
sys_greatbelt_aging_get_property(uint8 lchip, ctc_aging_table_type_t tbl_type, ctc_aging_prop_t aging_prop, uint32* value);

/**
 @brief The function is to set chip's aging status
*/
extern int32
sys_greatbelt_aging_set_aging_status(uint8 lchip, sys_aging_domain_type_t domain_type, uint32 key_index, bool enable);

/**
 @brief The function is to get chip's aging status
*/
extern int32
sys_greatbelt_aging_get_aging_status(uint8 lchip, uint32 domain_type, uint32 key_index, uint8* status);

/**
 @brief The function is to get chip's aging status
*/
extern int32
sys_greatbelt_aging_get_aging_index_status(uint8 lchip, uint32 fifo_ptr, ctc_aging_status_t* age_status);

/**
 @brief The function is to get key index by aging index
*/
extern int32
sys_greatbelt_aging_get_key_index(uint8 lchip, uint32 aging_index, uint8* domain_type, uint32* key_index);

/**
 @brief The function is to read chip's aging fifo,
        and remove FDB table according to Aging FIFO.
*/
extern int32
sys_greatbelt_aging_read_aging_fifo(uint8 lchip, ctc_aging_fifo_info_t* fifo_info_ptr);

/***************************************
 Init interfaces
*****************************************/
/**
 @brief The function will init learning and aging module
*/
extern int32
sys_greatbelt_learning_aging_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg);

/**
 @brief De-Initialize learning_aging module
*/
extern int32
sys_greatbelt_learning_aging_deinit(uint8 lchip);

/***************************************
 set event callback interfaces
*****************************************/
/**
 @brief Register user's callback function for CTC_EVENT_L2_SW_LEARNING event
*/
extern int32
sys_greatbelt_learning_set_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb);

/**
 @brief Register user's callback function for CTC_EVENT_L2_SW_AGING event
*/
extern int32
sys_greatbelt_aging_set_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb);

#ifdef __cplusplus
}
#endif

#endif /* !_SYS_GREATBELT_LEARNING_AGING_H */

