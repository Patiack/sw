/**
 @file sys_greatbelt_interrupt_priv.h

 @date 2012-10-31

 @version v2.0

*/
#ifndef _SYS_GREATBELT_INTERRUPT_PRIV_H
#define _SYS_GREATBELT_INTERRUPT_PRIV_H
#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_debug.h"
#include "ctc_const.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_INTR_DUMP(FMT, ...)             \
    do                                          \
    {                                           \
        CTC_DEBUG_OUT(interrupt, interrupt, INTERRUPT_SYS, CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_INTERRUPT_DBG_INFO(FMT, ...)    \
    do                                          \
    {                                           \
        CTC_DEBUG_OUT_INFO(interrupt, interrupt, INTERRUPT_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_INTERRUPT_DBG_FUNC()            \
    do                                          \
    {                                           \
        CTC_DEBUG_OUT_FUNC(interrupt, interrupt, INTERRUPT_SYS); \
    } while (0)

typedef int32 (* CTC_INTERRUPT_GROUP_FUNC)(uint32 group);

#define INTR_MUTEX  p_gb_intr_master[lchip]->p_intr_mutex

#define INTR_LOCK \
    if (INTR_MUTEX) sal_mutex_lock(INTR_MUTEX)
#define INTR_UNLOCK \
    if (INTR_MUTEX) sal_mutex_unlock(INTR_MUTEX)

#define COUNTOF(_array_)    ((uint32)(sizeof((_array_)) / sizeof((_array_)[0])))

typedef struct
{
    int32               group;
    uint32              intr;
    uint32              valid;
    uint32              occur_count;
    CTC_INTERRUPT_FUNC  isr;
    char                desc[CTC_INTR_DESC_LEN];
} sys_intr_t;

typedef struct
{
    int32               group;
    uint32              irq;
    int32               prio;
    uint32              intr_bmp;
    uint32              reg_bmp;
    uint32              intr_count;
    uint32              occur_count;
    char                desc[CTC_INTR_DESC_LEN];
} sys_intr_group_t;

typedef struct sys_intr_op_s
{
    int32 (* clear_status)(uint8 lchip, sys_intr_type_t* p_type, uint32 with_sup);
    int32 (* get_status)(uint8 lchip, sys_intr_type_t* p_type, ctc_intr_status_t* p_status);
    int32 (* set_en)(uint8 lchip, sys_intr_type_t* p_type, uint32 enable);
    int32 (* get_en)(uint8 lchip, sys_intr_type_t* p_type, uint32* p_enable);
} sys_intr_op_t;

typedef struct sys_intr_master_s
{
    uint32              is_default;
    uint32              group_count;
    uint32              intr_count;
    sys_intr_group_t    group[CTC_INTR_MAX_GROUP];
    sys_intr_t          intr[CTC_INTR_MAX_INTR];
    sys_intr_op_t*      op[CTC_INTR_MAX_INTR];
    CTC_INTERRUPT_EVENT_FUNC abnormal_intr_cb;
    sal_mutex_t*        p_intr_mutex;
    uint8  intr_mode;
    uint32 occur_count[CTC_INTERRUPT_FATAL_INTR_RESET+1];
} sys_intr_master_t;

extern sys_intr_master_t* p_gb_intr_master[CTC_MAX_LOCAL_CHIP_NUM];

/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

extern int32
sys_greatbelt_interrupt_get_default_global_cfg(uint8 lchip, sys_intr_global_cfg_t* p_intr_cfg);

#ifdef __cplusplus
}
#endif

#endif

