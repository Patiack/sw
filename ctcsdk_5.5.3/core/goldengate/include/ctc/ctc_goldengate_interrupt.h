/**
 @file ctc_goldengate_interrupt.h

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-10-23

 @version v2.0

 The interrupt is an asynchronous mechanism which allow hardware/chip to communicate with the software.
 For example, when learning cache in chip exceed threshold, chip notify software though interrupt;
 software received this interrupt, read out the entries in learning cache and notify application to add these MAC entries.
 There are two files for interrupt module:
 \p
 \d sys_goldengate_interrupt.c implements the APIs (init, set_en, get_en, clear_status, get_status) and the dispatch of groups and interrupts.
 \d sys_goldengate_interrupt_isr.c implements the default configurations of interrupt and the declaration of interrupt callback functions.
 \b
 User need to determine four things below.
 \p
 \d your application needs how many interrupt groups/PINs [0,5], and the IRQs and task priority of each group.
 \d your application needs how many sup-level interrupts [0,28], and the mapping to groups [0,5]
 \d your application needs register how many events (ctc_interrupt_event_t), and the callback function of registered events
 \d initialize interrupt module by parameter or by change group & interrupt default configuration in sys_goldengate_interrupt_isr.c
 \b

\S ctc_interrupt.h:ctc_interrupt_fatal_intr_action_t
\S ctc_interrupt.h:ctc_interrupt_ecc_type_t
\S ctc_interrupt.h:ctc_interrupt_type_t
\S ctc_interrupt.h:ctc_interrupt_event_t
\S ctc_interrupt.h:ctc_interrupt_ecc_t
\S ctc_interrupt.h:ctc_intr_type_t
\S ctc_interrupt.h:ctc_interrupt_abnormal_status_t

*/

#ifndef _CTC_GOLDENGATE_INTERRUPT_H
#define _CTC_GOLDENGATE_INTERRUPT_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
*
* Header Files
*
***************************************************************/
#include "ctc_interrupt.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/
/**
 @addtogroup interrupt INTERRUPT
 @{
*/


/****************************************************************
*
* Function
*
***************************************************************/

/**
 @brief Initialize interrupt module

 @param[in] lchip    local chip id

 @param[in] p_global_cfg    pointer to interrupt initialize parameter, refer to ctc_intr_global_cfg_t

 @remark  User can initialize the interrupt configuration though p_global_cfg parameter,  If the p_global_cfg is NULL,
         SDK will using default configuration.

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_init(uint8 lchip, void* p_global_cfg);


/**
 @brief De-Initialize interrupt module

 @param[in] lchip    local chip id

 @remark  User can de-initialize the interrupt configuration

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_deinit(uint8 lchip);

/**
 @brief Clear interrupt status

 @param[in] lchip_id    local chip id

 @param[in] p_type      Pointer to interrupt type

 @remark  User need to clear interrupt status after the reason (e.g. cache entry count exceed threshold) result in this interrupt is released

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_clear_status(uint8 lchip_id, ctc_intr_type_t* p_type);

/**
 @brief Get interrupt status

 @param[in] lchip_id    local chip id

 @param[in]  p_type     Pointer to interrupt type

 @param[out] p_status   Pointer to interrupt status

 @remark  Could read sup-level, sub-level, low-level interrupt status
\p
          sup-level: p_type->intr == CTC_INTR_ALL
\p
          sub-level: p_type->intr == ctc_interrupt_type_gb_t, p_type->sub_intr == CTC_INTR_ALL
\p
          low-level: p_type->intr == ctc_interrupt_type_gb_t, p_type->sub_intr == ctc_interrupt_type_gb_sub_xxx_t

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_get_status(uint8 lchip_id, ctc_intr_type_t* p_type, ctc_intr_status_t* p_status);

/**
 @brief Set interrupt enable

 @param[in] lchip_id    local chip id

 @param[in]  p_type     Pointer to interrupt type

 @param[in]  enable     TRUE or FALSE

 @remark  Enable/disable interrupt

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_set_en(uint8 lchip_id, ctc_intr_type_t* p_type, uint32 enable);

/**
 @brief Get interrupt enable

 @param[in] lchip_id    local chip id

 @param[in]  p_type     Interrupt type

 @param[out] p_enable   Pointer to enable, TRUE or FALSE

 @remark  Get interrupt enable status

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_get_en(uint8 lchip_id, ctc_intr_type_t* p_type, uint32* p_enable);

/**
 @brief Register event callback function

 @param[in] lchip    local chip id

 @param[in]  event      Event type

 @param[in]  cb         Callback function to handle this event type

 @remark  User register callback functions of application concerned events though this API

 @return CTC_E_XXX
*/
extern int32
ctc_goldengate_interrupt_register_event_cb(uint8 lchip, ctc_interrupt_event_t event, CTC_INTERRUPT_EVENT_FUNC cb);

/**@} end of @addtogroup interrupt INTERRUPT */

#ifdef __cplusplus
}
#endif

#endif

