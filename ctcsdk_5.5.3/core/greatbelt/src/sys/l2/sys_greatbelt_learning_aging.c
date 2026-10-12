/**
 @file ctc_greatbelt_learning_aging.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-10-23

 @version v2.0

 This file define sys functions

*/

/****************************************************************************
*
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"
#include "ctc_vector.h"
#include "ctc_learning_aging.h"
#include "ctc_interrupt.h"
#include "ctc_debug.h"
#include "ctc_interrupt.h"
#include "ctc_greatbelt_interrupt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_l2_fdb.h"
#include "sys_greatbelt_interrupt.h"
#include "sys_greatbelt_learning_aging.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
*
* Defines and Macros
*
*****************************************************************************/
#define SYS_LEARNING_CACHE_MAX_INDEX    16
#define SYS_AGING_BASE_SHIFT            8
#define SYS_AGING_STATUS_CHECK_BIT_LEN  16
#define SYS_AGING_DEFAULT_THRESHOLD     1
#define SYS_AGING_MIN_INTERVAL          1

#define SYS_TCAM_ENTRY_NUM              8192
#define SYS_LPM_TCAM_ENTRY_NUM          256
#define FDB_LOOKUP_INDEX_CAM_MAX_INDEX  16

/* minmum aging_ptr should be more than 1000000s * 550M / 0xFFFFFFFF, Note:550M is the frequency of chip */
#define SYS_AGING_MIN_AGING_PTR   (136 * 1024)

/**
 @brief enum type about aging scan width
*/
enum sys_aging_scan_width_e
{
    SYS_AGING_SCAN_WIDTH_16K = 0,   /**< aging scan range from 0~16K */
    SYS_AGING_SCAN_WIDTH_32K,       /**< aging scan range from 0~32K */
    SYS_AGING_SCAN_WIDTH_64K,       /**< aging scan range from 0~64K */
    SYS_AGING_SCAN_WIDTH_128K,      /**< aging scan range from 0~128K */
    SYS_AGING_SCAN_WIDTH_256K      /**< aging scan range from 0~256K */
};
typedef enum sys_aging_scan_width_e sys_aging_scan_width_t;

#define SYS_LEARNING_AGING_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(l2, learning_aging, L2_LEARNING_AGING_SYS, level, FMT, ## __VA_ARGS__); \
    } while (0);

/**
 @brief struct type about aging info
*/
struct sys_learning_aging_ds_s
{
    uint32 aging_interval[MAX_CTC_AGING_TBL];

    uint32 aging_tcam_max_ptr;
    uint32 aging_user_id_max_ptr;
    uint32 aging_ip_max_ptr;
    uint32 aging_mac_max_ptr;
    uint32 aging_maxptr;

    uint32 tcam_256_aging_base;
    uint32 tcam_8k_aging_base;
    uint32 userid_hash_aging_base;
    uint32 ip_hash_aging_base;
    uint32 mac_hash_aging_base;

    uint8 fifo_threshold;
    uint8 always_cpu_learning;
    uint8 learning_cache_int_threshold;
    uint8 learning_exception_en;
    uint8 aps_select_en;
    uint8 mac_hash_conflict_learning_disable;
    uint8 fdb_ad_type;

    sal_task_t* t_sw_learning;
    sal_task_t* t_sw_aging;
    sal_sem_t*  sw_learning_sem;
    sal_sem_t*  sw_aging_sem;

    CTC_INTERRUPT_EVENT_FUNC sw_learning_cb;
    CTC_INTERRUPT_EVENT_FUNC sw_aging_cb;
};
typedef struct sys_learning_aging_ds_s sys_learning_aging_ds_t;

/****************************************************************************
*
* Global and Declaration
*
*****************************************************************************/
static sys_learning_aging_ds_t* g_learning_aging_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_LEARNING_AGING_INIT_CHECK(lchip)             \
    do {                                \
        SYS_LCHIP_CHECK_ACTIVE(lchip);       \
        if (NULL == g_learning_aging_master[lchip])  \
        { return CTC_E_NOT_INIT; }      \
    } while(0)

sal_mutex_t * learning_mutex = NULL;
sal_mutex_t * aging_mutex = NULL;
#define LEARNING_LOCK \
    if (learning_mutex) sal_mutex_lock(learning_mutex);
#define LEARNING_UNLOCK \
    if (learning_mutex) sal_mutex_unlock(learning_mutex);


#define AGING_LOCK \
    if (aging_mutex) sal_mutex_lock(aging_mutex);
#define AGING_UNLOCK \
    if (aging_mutex) sal_mutex_unlock(aging_mutex);

/****************************************************************************
*
* Function
*
*****************************************************************************/
STATIC int32
_sys_greatbelt_learning_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg)
{
    ipe_learning_cache_valid_t learn_cache_vld;
    drv_work_platform_type_t platform_type;
    uint32 cmd = 0;
    uint32 cmd_hash_conflict = 0;
    uint32 field_val = 0;
    uint32 mac_hash_conflict_learning_disable = 0;

    /*
    IpeLearningCacheValid.exceptionEn = 1:
    When alway cpu learning, set exceptionEn, then each needed learning pkt will send to cpu.
    When interrupt learning, set exceptionEn, cache full, then remained need learning pkt will send to cpu.
    */
    cmd = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);

    sal_memset(&learn_cache_vld, 0, sizeof(ipe_learning_cache_valid_t));

    learn_cache_vld.always_cpu_learning = 0;
    learn_cache_vld.exception_en = 0;
    learn_cache_vld.learning_cache_int_threshold = 0;
    learn_cache_vld.learning_entry_valid = 0xFFFF;  /*must write one clear */

    drv_greatbelt_get_platform_type(&platform_type);
    if (platform_type == HW_PLATFORM)
    {
        learn_cache_vld.learning_entry_valid = 0xFFFF;  /*must write one clear */
    }
    else
    {
        learn_cache_vld.learning_entry_valid = 0;  /*must write one clear */
    }

    cmd_hash_conflict = DRV_IOW(IpeLearningCacheValid_t, IpeLearningCacheValid_MacHashConflictLearningDisable_f);
    mac_hash_conflict_learning_disable = 1;


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd_hash_conflict, &mac_hash_conflict_learning_disable));

    g_learning_aging_master[lchip] =
    (sys_learning_aging_ds_t*)mem_malloc(MEM_FDB_MODULE, sizeof(sys_learning_aging_ds_t));
    if (NULL == g_learning_aging_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(g_learning_aging_master[lchip], 0, sizeof(sys_learning_aging_ds_t));

    g_learning_aging_master[lchip]->mac_hash_conflict_learning_disable = 1;
    g_learning_aging_master[lchip]->always_cpu_learning = 0;
    g_learning_aging_master[lchip]->learning_cache_int_threshold = 0;
    g_learning_aging_master[lchip]->learning_exception_en = 0;

    cmd = DRV_IOR(DynamicDsFdbHashCtl_t, DynamicDsFdbHashCtl_AdBitsType_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    g_learning_aging_master[lchip]->fdb_ad_type = field_val;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_learning_do_sw_learning(uint8 lchip)
{
    drv_work_platform_type_t platform_type;
    ipe_learning_cache_valid_t learn_cache_vld;
    uint32 cmd = 0;
    uint8 gchip = 0;
    uint16 entry_vld_bitmap = 0;
    ctc_learning_cache_t* p_cache = NULL;
    int32 ret = CTC_E_NONE;
    uint8 count = 200;
    sal_systime_t tv1,tv2;

    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));

    p_cache = (ctc_learning_cache_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(ctc_learning_cache_t));
    if (NULL == p_cache)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_cache, 0, sizeof(ctc_learning_cache_t));

    sal_gettime(&tv1);
    while(count)
    {
        /* 1. read valid bitmap */
        cmd = DRV_IOR(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld), ret, out);
        entry_vld_bitmap = learn_cache_vld.learning_entry_valid;

        if(learn_cache_vld.learning_entry_valid == 0)
        {
            sal_gettime(&tv2);
            SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "[Learning] AgingPtr:time1:[0x%x:0x%x],time2:[0x%x:0x%x] time:[0x%x:0x%x]\n",
                                       tv1.tv_sec,tv1.tv_usec,tv2.tv_sec,tv2.tv_usec,tv2.tv_sec-tv1.tv_sec,tv2.tv_usec-tv1.tv_usec);

            break;
        }

        /* 2. read learning cache */
        CTC_ERROR_GOTO(sys_greatbelt_learning_read_learning_cache(lchip, entry_vld_bitmap, p_cache), ret, out);

        /* 3. call event callback function */
        if (g_learning_aging_master[lchip]->sw_learning_cb)
        {

            ret = sys_greatbelt_get_gchip_id(lchip, &gchip);
            if (0 == ret)
            {
                LEARNING_LOCK;
                g_learning_aging_master[lchip]->sw_learning_cb(gchip, p_cache);
                LEARNING_UNLOCK;
            }

        }

        /* 4. clear valid bitmap */
        if (platform_type != HW_PLATFORM)
        {
            if (!drv_greatbelt_io_is_asic())
            {
                learn_cache_vld.learning_entry_valid = 0;
            }
        }

        cmd = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld), ret, out);

        count--;
    }

out:
    if (p_cache)
    {
        mem_free(p_cache);
    }

    return ret;
}

STATIC void
_sys_greatbelt_learning_thread(void* param)
{
    gb_sup_interrupt_function_t sup_func;
    uint32 cmd = 0;
    ctc_intr_type_t type;
    int32 ret = 0;
    uint8 lchip = (uintptr)param;

    sal_memset(&type, 0, sizeof(ctc_intr_type_t));
    sal_memset(&sup_func, 0, sizeof(gb_sup_interrupt_function_t));
    type.intr = CTC_INTR_GB_FUNC_IPE_LEARN_CACHE;
    type.sub_intr = 0;
    CTC_BIT_SET(sup_func.func_intr, CTC_INTR_GB_FUNC_IPE_LEARN_CACHE - CTC_INTR_GB_FUNC_PTP_TS_CAPTURE);
    while (1)
    {
        ret = sal_sem_take(g_learning_aging_master[lchip]->sw_learning_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        _sys_greatbelt_learning_do_sw_learning(lchip);
        /* clear interrupt */
        cmd = DRV_IOW(GbSupInterruptFunction_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &sup_func);
        sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);
    }

    return;
}

int32
sys_greatbelt_learning_set_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];
    ipe_learning_cache_valid_t learn_cache_vld;
    uint32 cmd = 0;
    uint32 field_val = 0;

    CTC_PTR_VALID_CHECK(p_learning_action);

    if(p_learning_action->action!=CTC_LEARNING_ACTION_MAC_TABLE_FULL)
    {
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "Set learning cache threshold: %d, action: %d\n",
                                   p_learning_action->value, p_learning_action->action);
    }

    sal_memset(&learn_cache_vld, 0, sizeof(learn_cache_vld));

    cmd = DRV_IOR(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));

    switch (p_learning_action->action)
    {
    case CTC_LEARNING_ACTION_ALWAYS_CPU:
        learn_cache_vld.always_cpu_learning = TRUE;
        learn_cache_vld.exception_en = TRUE;
        break;

    case CTC_LEARNING_ACTION_CACHE_FULL_TO_CPU:
        learn_cache_vld.always_cpu_learning = FALSE;
        learn_cache_vld.exception_en = TRUE;
        if (p_learning_action->value >= SYS_LEARNING_CACHE_MAX_INDEX)
        {
            SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Learning threshold cfg exceeds the max value 0x10!\n");
            return CTC_E_LEARNING_AND_AGING_INVALID_LEARNING_THRESHOLD;
        }

        learn_cache_vld.learning_cache_int_threshold = p_learning_action->value & 0x1F;
        break;

    case CTC_LEARNING_ACTION_CACHE_ONLY:
        learn_cache_vld.always_cpu_learning = FALSE;
        learn_cache_vld.exception_en = FALSE;
        if (p_learning_action->value >= SYS_LEARNING_CACHE_MAX_INDEX)
        {
            SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Learning threshold cfg exceeds the max value 0x10!\n");
            return CTC_E_LEARNING_AND_AGING_INVALID_LEARNING_THRESHOLD;
        }

        learn_cache_vld.learning_cache_int_threshold = p_learning_action->value & 0x1F;
        break;

    case CTC_LEARNING_ACTION_DONLEARNING:
        learn_cache_vld.always_cpu_learning = TRUE;
        learn_cache_vld.exception_en = FALSE;
        break;

    case CTC_LEARNING_ACTION_MAC_TABLE_FULL:
        cmd = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_MacTableFull_f);
        field_val = (p_learning_action->value) ? 1 : 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        return CTC_E_NONE;
    case CTC_LEARNING_ACTION_MAC_HASH_CONFLICT_LEARNING_DISABLE:
        cmd = DRV_IOW(IpeLearningCacheValid_t, IpeLearningCacheValid_MacHashConflictLearningDisable_f);
        field_val = (p_learning_action->value) ? 1 : 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        p_master->mac_hash_conflict_learning_disable = field_val;
        return CTC_E_NONE;
    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));
    p_master->always_cpu_learning = learn_cache_vld.always_cpu_learning;
    p_master->learning_exception_en = learn_cache_vld.exception_en;
    p_master->learning_cache_int_threshold = learn_cache_vld.learning_cache_int_threshold;

    return CTC_E_NONE;
}

int32
sys_greatbelt_learning_get_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action)
{
    uint32 cmd = 0;
    ipe_learning_cache_valid_t learn_cache_vld;

    CTC_PTR_VALID_CHECK(p_learning_action);

    sal_memset(&learn_cache_vld, 0, sizeof(learn_cache_vld));
    cmd = DRV_IOR(IpeLearningCacheValid_t,  DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));

    if (TRUE == learn_cache_vld.exception_en)
    {
        if (TRUE == learn_cache_vld.always_cpu_learning)
        {
            p_learning_action->action = CTC_LEARNING_ACTION_ALWAYS_CPU;
        }
        else
        {
            p_learning_action->action = CTC_LEARNING_ACTION_CACHE_FULL_TO_CPU;
            p_learning_action->value = learn_cache_vld.learning_cache_int_threshold & 0x1F;
        }
    }
    else
    {
        if (FALSE == learn_cache_vld.always_cpu_learning)
        {
            p_learning_action->action = CTC_LEARNING_ACTION_CACHE_ONLY;
            p_learning_action->value = learn_cache_vld.learning_cache_int_threshold & 0x1F;
        }
        else
        {
            p_learning_action->action = CTC_LEARNING_ACTION_DONLEARNING;
        }
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_learning_get_cache_entry_valid_bitmap(uint8 lchip, uint16* entry_vld_bitmap)
{
    uint32 cmd = 0;
    ipe_learning_cache_valid_t learn_cache_vld;

    cmd = DRV_IOR(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));
    *entry_vld_bitmap = learn_cache_vld.learning_entry_valid & 0xFFFF;

    return CTC_E_NONE;
}

int32
sys_greatbelt_learning_read_learning_cache(uint8 lchip,
                                           uint16 entry_vld_bitmap,
                                           ctc_learning_cache_t* l2_lc)
{
    ipe_learning_cache_t learning_cache;
    uint32 cmd = 0;
    uint8 index = 0;
    uint8 entry_idx = 0;

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Learning] Read learning cache data: \n");

    /* check PARA */
    CTC_PTR_VALID_CHECK(l2_lc);

    l2_lc->entry_num = 0;
    if (0 == entry_vld_bitmap)
    {
        return CTC_E_NONE;
    }

    sal_memset(&learning_cache, 0, sizeof(ipe_learning_cache_t));
    cmd = DRV_IOR(IpeLearningCache_t, DRV_ENTRY_FLAG);

    for (index = 0, entry_idx = 0; index < SYS_LEARNING_CACHE_MAX_INDEX; index++)
    {
        /* Get info from the learning cache */
        if (entry_vld_bitmap & (1 << index))
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &learning_cache));

            if (learning_cache.is_global_src_port)
            {
                SYS_GREATBELT_GPORT14_TO_GPORT(learning_cache.source_port);
            }

            l2_lc->learning_entry[entry_idx].cvlan_id = learning_cache.old_cvlan_id;
            l2_lc->learning_entry[entry_idx].is_logic_port = (!learning_cache.is_global_src_port);
            l2_lc->learning_entry[entry_idx].is_ether_oam = learning_cache.is_ether_oam;
            l2_lc->learning_entry[entry_idx].svlan_id = learning_cache.old_svlan_id;
            l2_lc->learning_entry[entry_idx].ether_oam_md_level = learning_cache.ether_oam_level;
            l2_lc->learning_entry[entry_idx].global_src_port = learning_cache.source_port;
            l2_lc->learning_entry[entry_idx].logic_port = learning_cache.source_port;
            l2_lc->learning_entry[entry_idx].mapped_vlan_id = learning_cache.new_svlan_id;
            l2_lc->learning_entry[entry_idx].mac_sa_32to47 = learning_cache.mac_sa_hi;
            l2_lc->learning_entry[entry_idx].mac_sa_0to31 = learning_cache.mac_sa_low;
            l2_lc->learning_entry[entry_idx].fid = learning_cache.vsi_id;
            entry_idx++;
        }
    }

    l2_lc->entry_num = entry_idx;

    return CTC_E_NONE;
}

int32
sys_greatbelt_learning_clear_learning_cache(uint8 lchip, uint16 entry_vld_bitmap)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];
    ipe_learning_cache_valid_t learn_cache_vld;
    uint32 cmd = 0;
    drv_work_platform_type_t platform_type;

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Learning] Clear learning cache data: \n");

    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));

    learn_cache_vld.always_cpu_learning = p_master->always_cpu_learning;
    learn_cache_vld.exception_en = p_master->learning_exception_en;
    learn_cache_vld.learning_cache_int_threshold = p_master->learning_cache_int_threshold;
    learn_cache_vld.mac_hash_conflict_learning_disable = p_master->mac_hash_conflict_learning_disable;

    learn_cache_vld.learning_entry_valid = entry_vld_bitmap;  /*must write one clear */
    if (platform_type != HW_PLATFORM)
    {
        if (!drv_greatbelt_io_is_asic())
        {
            learn_cache_vld.learning_entry_valid = 0;
        }
    }

    cmd = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));

    return CTC_E_NONE;
}

/**
 @brief ISR to handle CTC_INTR_GB_FUNC_IPE_LEARN_CACHE interrupt
*/
int32
sys_greatbelt_learning_isr(uint8 lchip, uint32 intr, void* p_data)
{
    ctc_intr_type_t type;
    sal_memset(&type, 0, sizeof(ctc_intr_type_t));
    type.intr = CTC_INTR_GB_FUNC_IPE_LEARN_CACHE;
    type.sub_intr = 0;
    sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);
    if (g_learning_aging_master[lchip])
    {
        sal_sem_give(g_learning_aging_master[lchip]->sw_learning_sem);
    }
    return CTC_E_NONE;
}

/**
 @brief Register user's callback function for CTC_EVENT_L2_SW_LEARNING event
*/
int32
sys_greatbelt_learning_set_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    g_learning_aging_master[lchip]->sw_learning_cb = cb;
    return CTC_E_NONE;
}

/**
 @brief set aging interval
*/
STATIC int32
_sys_greatbelt_aging_set_aging_interval(uint8 lchip, ctc_aging_table_type_t tbl_type, uint32 age_seconds)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];
    uint32 cmd = 0;
    uint32 aging_interval = 0;
    uint32 max_age_seconds = 0;
    uint32 core_frequecy = 0;
    uint8  timer_index = 0;

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Aging] Set aging interval:tbl_type: %d, age_seconds:%d\n", tbl_type, age_seconds);


    if (tbl_type == CTC_AGING_TBL_SCL)
    {
        timer_index = 1;
    }

    core_frequecy = sys_greatbelt_get_core_freq(0);

    max_age_seconds = ((CTC_MAX_UINT32_VALUE / 1000000) * p_master->aging_maxptr) / core_frequecy;

    if (age_seconds < SYS_AGING_MIN_INTERVAL || age_seconds > max_age_seconds)
    {
        return CTC_E_AGING_INVALID_INTERVAL;
    }

    aging_interval = age_seconds * ((core_frequecy * 1000000) / p_master->aging_maxptr);
    cmd = DRV_IOW(IpeAgingCtl_t, (IpeAgingCtl_AgingInterval0_f + timer_index));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aging_interval));
    p_master->aging_interval[timer_index] = age_seconds;
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] Set aging aging_interval: %u max_age_seconds:%u \n", aging_interval, max_age_seconds);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_aging_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];
    ipe_aging_ctl_t ipe_aging_ctl;
    uint32 entry_num = 0;
    uint32 field_val = 0;
    uint32 max_ptr = 0;
    uint32 ip_aging_num = 0;
    uint32 cmd = 0;
    uint32 core_frequency = 0;
    uint32 deta     = 0;


    sal_memset(&ipe_aging_ctl, 0, sizeof(ipe_aging_ctl_t));

    cmd = DRV_IOR(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ctl));

    /*1. Aging for TCAM*/
    p_master->tcam_8k_aging_base = 0;
    max_ptr += SYS_TCAM_ENTRY_NUM;

    /*2. Aging for LpmTCAM*/
    p_master->tcam_256_aging_base = max_ptr / 8;
    max_ptr += SYS_LPM_TCAM_ENTRY_NUM;
    p_master->aging_tcam_max_ptr = max_ptr;

    /*3. Aging for UserID*/
    p_master->userid_hash_aging_base = max_ptr / 8;
    if (cfg->hw_scl_aging_en)
    {
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsUserIdCvlanCosHashKey_t, &entry_num);
    }

    max_ptr += entry_num;
    ipe_aging_ctl.aging_user_id_max_ptr = max_ptr;
    p_master->aging_user_id_max_ptr = max_ptr;

    /*4. Aging for FIB IP*/
    if (cfg->ipmc_aging_en)
    {
        sys_greatbelt_ftm_get_table_entry_num(lchip, DsLpmIpv4McastHash32Key_t, &entry_num);
        ip_aging_num = entry_num;
    }

    p_master->ip_hash_aging_base = p_master->userid_hash_aging_base + entry_num / 32 /*UserIdEntryNum*/;

    max_ptr += entry_num;
    ipe_aging_ctl.aging_ip_max_ptr = max_ptr;   /*disable ipmc aging */
    p_master->aging_ip_max_ptr = max_ptr;

    /*5. Aging for FIB MAC*/
    p_master->mac_hash_aging_base = p_master->ip_hash_aging_base + entry_num / 32 /*ipEntryNum*/;
    entry_num = 0;
    sys_greatbelt_ftm_get_table_entry_num(lchip, DsMacHashKey_t, &entry_num);
    entry_num += FDB_LOOKUP_INDEX_CAM_MAX_INDEX;

    max_ptr += entry_num;
    ipe_aging_ctl.aging_mac_max_ptr = max_ptr;
    p_master->aging_mac_max_ptr = max_ptr;

    /*config aging Ptr */
    ipe_aging_ctl.aging_ptr0 = 0;
    ipe_aging_ctl.aging_ptr1 = 0;
    ipe_aging_ctl.min_ptr = 0;

    /* TODO */
    core_frequency = sys_greatbelt_get_core_freq(0);
    max_ptr = (max_ptr > SYS_AGING_MIN_AGING_PTR) ? max_ptr : SYS_AGING_MIN_AGING_PTR;
    deta = (core_frequency * 1000000)/max_ptr;
    max_ptr = (core_frequency * 1000000)/deta;
    ipe_aging_ctl.max_ptr = max_ptr - 1;
    p_master->aging_maxptr = max_ptr;

    ipe_aging_ctl.scan_pause_on_fifo_full = FALSE;
    ipe_aging_ctl.software_read_clear = FALSE;
    ipe_aging_ctl.stop_on_max_ptr = FALSE;
    ipe_aging_ctl.fifo_depth_threshold = 1;
    ipe_aging_ctl.mac_ipv4_sa_en = FALSE;
    ipe_aging_ctl.aging_index_valid = TRUE;

    ipe_aging_ctl.scan_en0 = FALSE;
    ipe_aging_ctl.scan_en1 = FALSE;
    ipe_aging_ctl.scan_en2 = FALSE;
    ipe_aging_ctl.scan_en3 = FALSE;


    cmd = DRV_IOW(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ctl));

    /*aging interval  */
    CTC_ERROR_RETURN(_sys_greatbelt_aging_set_aging_interval(lchip, CTC_AGING_TBL_MAC, 30));  /*30s*/
    CTC_ERROR_RETURN(_sys_greatbelt_aging_set_aging_interval(lchip, CTC_AGING_TBL_SCL, 30));  /*30s*/

    field_val = FALSE;
    cmd = DRV_IOW(IpeAgingCtl_t, IpeAgingCtl_ScanEn0_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0; /*Hash and Ltcam UserId AgingEn*/
    cmd = DRV_IOW(UserIdResultCtl_t, UserIdResultCtl_AgingEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*Hash Mac Sa AgingEn*/
    field_val = 0;
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_MacSaLookupResultCtl_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val |= (1 << 11);
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_MacSaLookupResultCtl_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*Hash IPv4 mcast da AgingEn*/
    field_val = 0;
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val |= (1 << 11);
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl1_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /*Hash IPv6 mcast da AgingEn*/
    field_val = 0;
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val |= (1 << 11);
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_IpDaLookupResultCtl3_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0; /*Tcam Mac AgingEn*/
    cmd = DRV_IOW(TcamEngineLookupResultCtl2_t, TcamEngineLookupResultCtl2_MacSaAgingEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = ip_aging_num; /*Mac AgingBase*/
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, FibEngineLookupResultCtl_FdbAgingBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_aging_get_aging_index(uint8 lchip, uint8 domain_type, uint32 key_index, uint32* aging_status_index)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];

    switch (domain_type)
    {
    case SYS_AGING_TCAM_256:
        *aging_status_index = p_master->tcam_256_aging_base + (key_index / 8);
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingPtr:0x%x \n",
                                   key_index + SYS_TCAM_ENTRY_NUM);
        break;

    case SYS_AGING_TCAM_8k:
        *aging_status_index = p_master->tcam_8k_aging_base + (key_index / 8);
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingPtr:0x%x \n", key_index);
        break;

    case SYS_AGING_USERID_HASH:
        *aging_status_index = p_master->userid_hash_aging_base + (key_index / 32);
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingPtr:0x%x \n",
                                   key_index + SYS_TCAM_ENTRY_NUM + SYS_LPM_TCAM_ENTRY_NUM);
        break;

    case SYS_AGING_MAC_HASH:
        *aging_status_index = p_master->mac_hash_aging_base + (key_index / 32);
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingPtr:0x%x \n",
                                   key_index + p_master->aging_ip_max_ptr);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_aging_do_sw_aging(uint8 lchip)
{
    ctc_aging_fifo_info_t* aging_fifo = NULL;
    uint8 gchip = 0;
    int32 ret = CTC_E_NONE;
    uint8 count = 100;
    sal_systime_t tv1,tv2;

    aging_fifo = (ctc_aging_fifo_info_t*)mem_malloc(MEM_FDB_MODULE, sizeof(ctc_aging_fifo_info_t));
    if (NULL == aging_fifo)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(aging_fifo, 0, sizeof(ctc_aging_fifo_info_t));
    sal_gettime(&tv1);

    while(count)
    {
        /* 1. read aging FIFO */
        sys_greatbelt_aging_read_aging_fifo(lchip, aging_fifo);

        if(aging_fifo->fifo_idx_num == 0)
        {
            break;
        }

        /* 2. call event callback function */
        if (g_learning_aging_master[lchip]->sw_aging_cb)
        {
            ret = sys_greatbelt_get_gchip_id(lchip, &gchip);
            if (0 == ret)
            {
                AGING_LOCK;
                g_learning_aging_master[lchip]->sw_aging_cb(gchip, aging_fifo);
                AGING_UNLOCK;
            }
        }

        count--;
    }

    mem_free(aging_fifo);

    sal_gettime(&tv2);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingPtr:time1:[0x%x:0x%x],[0x%x:0x%x] \n",
                                   tv1.tv_sec,tv1.tv_usec,tv2.tv_sec,tv2.tv_usec);

    return CTC_E_NONE;
}

STATIC void
_sys_greatbelt_aging_thread(void* param)
{
    gb_sup_interrupt_function_t sup_func;
    uint32 cmd = 0;
    ctc_intr_type_t type;
    int32 ret = 0;
    uint8 lchip = (uintptr)param;

    sal_memset(&sup_func, 0, sizeof(gb_sup_interrupt_function_t));
    sal_memset(&type, 0, sizeof(ctc_intr_type_t));
    type.intr = CTC_INTR_GB_FUNC_IPE_AGING_FIFO;
    type.sub_intr = 0;
    CTC_BIT_SET(sup_func.func_intr, CTC_INTR_GB_FUNC_IPE_AGING_FIFO - CTC_INTR_GB_FUNC_PTP_TS_CAPTURE);
    while (1)
    {
        ret = sal_sem_take(g_learning_aging_master[lchip]->sw_aging_sem, SAL_SEM_FOREVER);
        if (0 != ret)
        {
            continue;
        }
        SYS_LCHIP_CHECK_ACTIVE_IN_THREAD(lchip);

        _sys_greatbelt_aging_do_sw_aging(lchip);
        /* clear interrupt */
        cmd = DRV_IOW(GbSupInterruptFunction_t, DRV_ENTRY_FLAG);
        DRV_IOCTL(lchip, INTR_INDEX_VAL_RESET, cmd, &sup_func);
        sys_greatbelt_interrupt_set_en(lchip, &type, TRUE);

    }

    return;
}

/**
 @brief This function is to set the aging properties
*/
int32
sys_greatbelt_aging_set_property(uint8 lchip, ctc_aging_table_type_t tbl_type, ctc_aging_prop_t aging_prop, uint32 value)
{
    uint32 cmd = 0;
    uint16 field = 0;
    uint8  timer_index = 0;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Aging] Set aging property:tbl_type %d, aging_prop:%d, value:%d\n", tbl_type, aging_prop, value);

    if (tbl_type == CTC_AGING_TBL_SCL)
    {
        timer_index = 1;
    }

    switch (aging_prop)
    {
    case CTC_AGING_PROP_FIFO_THRESHOLD:
        field = IpeAgingCtl_FifoDepthThreshold_f;
        CTC_MAX_VALUE_CHECK(value, 0xF);
        timer_index = 0;
        break;

    case CTC_AGING_PROP_INTERVAL:
        field = IpeAgingCtl_AgingInterval0_f;
        CTC_ERROR_RETURN(_sys_greatbelt_aging_set_aging_interval(lchip, tbl_type, value));
        return CTC_E_NONE;
        break;

    case CTC_AGING_PROP_STOP_SCAN_TIMER_EXPIRED:
        field = IpeAgingCtl_StopOnMaxPtr_f;
        timer_index = 0;
        value = value ? TRUE : FALSE;
        break;

    case CTC_AGING_PROP_AGING_SCAN_EN:
        field = IpeAgingCtl_ScanEn0_f;
        value = value ? TRUE : FALSE;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(IpeAgingCtl_t, field + timer_index);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

    return CTC_E_NONE;
}

/**
 @brief This function is to get the aging properties
*/
int32
sys_greatbelt_aging_get_property(uint8 lchip, ctc_aging_table_type_t tbl_type, ctc_aging_prop_t aging_prop, uint32* value)
{
    uint32 cmd = 0;
    uint16 field = 0;
    uint8  timer_index = 0;

    CTC_PTR_VALID_CHECK(value);
    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Aging] Get aging property:tbl_type %d, aging_prop:%d\n", tbl_type, aging_prop);

    if (tbl_type == CTC_AGING_TBL_SCL)
    {
        timer_index = 1;
    }

    switch (aging_prop)
    {
    case CTC_AGING_PROP_FIFO_THRESHOLD:
        field = IpeAgingCtl_FifoDepthThreshold_f;
        timer_index = 0;
        break;

    case CTC_AGING_PROP_INTERVAL:
        field = IpeAgingCtl_AgingInterval0_f;
        break;

    case CTC_AGING_PROP_STOP_SCAN_TIMER_EXPIRED:
        field = IpeAgingCtl_StopOnMaxPtr_f;
        timer_index = 0;
        break;

    case CTC_AGING_PROP_AGING_SCAN_EN:
        field = IpeAgingCtl_ScanEn0_f;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOR(IpeAgingCtl_t, field + timer_index);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    if (aging_prop == CTC_AGING_PROP_INTERVAL)
    {
        (*value) = g_learning_aging_master[lchip]->aging_interval[timer_index];
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_aging_get_key_index(uint8 lchip, uint32 aging_ptr, uint8* domain_type, uint32* key_index)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];

    CTC_PTR_VALID_CHECK(domain_type);
    CTC_PTR_VALID_CHECK(key_index);

    if (aging_ptr < p_master->aging_tcam_max_ptr)
    {
        if (aging_ptr < SYS_TCAM_ENTRY_NUM)
        {
            *key_index = aging_ptr;
            *domain_type = SYS_AGING_TCAM_8k;
        }
        else
        {
            *key_index = aging_ptr - SYS_TCAM_ENTRY_NUM;
            *domain_type = SYS_AGING_TCAM_256;
        }
    }
    else if (aging_ptr < p_master->aging_user_id_max_ptr)
    {
        *key_index = aging_ptr - p_master->aging_tcam_max_ptr;
        *domain_type = SYS_AGING_USERID_HASH;
    }
    else if (aging_ptr < p_master->aging_ip_max_ptr)
    {
        *key_index = aging_ptr - p_master->aging_user_id_max_ptr;
        *domain_type = SYS_AGING_LPM_HASH;
    }
    else if (aging_ptr < p_master->aging_mac_max_ptr)
    {
        *key_index = aging_ptr - p_master->aging_ip_max_ptr;
        *domain_type = SYS_AGING_MAC_HASH;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/* key_index for Tcam(0-8191), for lpmTcam(0-255), for edram(0-?)*/
int32
sys_greatbelt_aging_set_aging_status(uint8 lchip, sys_aging_domain_type_t domain_type, uint32 key_index, bool enable)
{
    ds_aging_t ds_aging;
    uint32 cmd = 0;
    uint32 aging_status_index = 0;
    uint8  aging_bit = 0;
    uint8  timer_idx = 0;
    uint8  offset = 0;

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] key_index:%d, domain_type:%d, enable:%d\n",
                               key_index, domain_type, enable);

    CTC_ERROR_RETURN(_sys_greatbelt_aging_get_aging_index(lchip, domain_type, key_index, &aging_status_index));

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingIndex:0x%x \n", aging_status_index);

    cmd = DRV_IOR(DsAging_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aging_status_index, cmd, &ds_aging));

    if (SYS_AGING_TCAM_8k == domain_type || SYS_AGING_TCAM_256 == domain_type)
    {
        /*tcam_8k or 256 lpm tcam*/
        timer_idx = 0;
        offset = (key_index % 8) * 4;
        ds_aging.aging_status &= (~(0xF << offset));  /*clear 4bit value*/
        if (enable)
        {
            aging_bit = (timer_idx & 0x3) << 2 | 0x3;  /* timer = 0  status = 1  valid = 1,*/
        }
        else
        {
            aging_bit = 0;   /*set status = 0, and valid = 0*/
        }

        ds_aging.aging_status |= (aging_bit << offset);
    }
    else
    {
        if (enable)
        {
            SET_BIT(ds_aging.aging_status, (key_index % 32));
        }
        else
        {
            CLEAR_BIT(ds_aging.aging_status, (key_index % 32));
        }
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingBitSet:0x%x , hit:%d \n", (key_index % 32), enable);
    }

    cmd = DRV_IOW(DsAging_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aging_status_index, cmd, &ds_aging));

    return CTC_E_NONE;
}


int32
sys_greatbelt_aging_get_aging_status(uint8 lchip, uint32 domain_type, uint32 key_index, uint8 *status)
{
    ds_aging_t ds_aging;
    uint32 cmd = 0;
    uint32 aging_status_index = 0;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] key_index:%d, domain_type:%d\n", key_index, domain_type);

    CTC_ERROR_RETURN(_sys_greatbelt_aging_get_aging_index(lchip, domain_type, key_index, &aging_status_index));

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingIndex:0x%x \n", aging_status_index);

    cmd = DRV_IOR(DsAging_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aging_status_index, cmd, &ds_aging));

    if (SYS_AGING_TCAM_8k == domain_type || SYS_AGING_TCAM_256 == domain_type)
    {
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] Tcam Aging not support \n");
        *status = 0;
    }
    else
    {
        *status = CTC_IS_BIT_SET(ds_aging.aging_status, (key_index % 32));
        SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingStatus:%d \n", *status);
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_aging_read_aging_fifo(uint8 lchip, ctc_aging_fifo_info_t* fifo_info)
{
    drv_work_platform_type_t platform_type;
    ds_aging_ptr_fifo_depth_t aging_fifo_depth;
    ds_aging_ptr_t ipe_aging_ptr;
    uint32 cmd = 0;
    uint32 key_index = 0;
    uint32 aging_ptr = 0;
    int32  ret = 0;
    uint8  domain_type = 0;
    uint8  fifo_idx = 0;
    uint8  index = 0;

    CTC_PTR_VALID_CHECK(fifo_info);

    cmd = DRV_IOR(DsAgingPtrFifoDepth_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &aging_fifo_depth);

    /* Get aging index from aging FIFO according CTC_AGING_FIFO_DEPTH*/
    for (index = 0; index < aging_fifo_depth.ds_aging_ptr_fifo_depth; index++)
    {
        cmd = DRV_IOR(DsAgingPtr_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ptr);
        if (ret < 0)
        {
            continue;
        }

        aging_ptr = ipe_aging_ptr.aging_ptr_found;

        ret = sys_greatbelt_aging_get_key_index(lchip, aging_ptr, &domain_type, &key_index);
        if (ret < 0)
        {
            continue;
        }

        fifo_info->aging_index_array[fifo_idx] = key_index;
        if (SYS_AGING_TCAM_256 == domain_type ||
            SYS_AGING_TCAM_8k == domain_type ||
            SYS_AGING_MAC_HASH == domain_type)
        {
            fifo_info->tbl_type[fifo_idx] = CTC_AGING_TBL_MAC;
        }

        if (SYS_AGING_USERID_HASH == domain_type)
        {
            fifo_info->tbl_type[fifo_idx] = CTC_AGING_TBL_SCL;
        }

        fifo_idx++;

    }

    fifo_info->fifo_idx_num = fifo_idx;
    drv_greatbelt_get_platform_type(&platform_type);
    if (platform_type != HW_PLATFORM)
    {
        aging_fifo_depth.ds_aging_ptr_fifo_depth = 0;
        cmd = DRV_IOW(DsAgingPtrFifoDepth_t, DRV_ENTRY_FLAG);
        ret = DRV_IOCTL(lchip, 0, cmd, &aging_fifo_depth);
    }

    return CTC_E_NONE;
}

/**
 @brief ISR to handle CTC_INTR_GB_FUNC_IPE_AGING_FIFO interrupt
*/
int32
sys_greatbelt_aging_isr(uint8 lchip, uint32 intr, void* p_data)
{
    ctc_intr_type_t type;
    sal_memset(&type, 0, sizeof(ctc_intr_type_t));
    type.intr = CTC_INTR_GB_FUNC_IPE_AGING_FIFO;
    type.sub_intr = 0;
    sys_greatbelt_interrupt_set_en(lchip, &type, FALSE);
    if (g_learning_aging_master[lchip])
    {
        sal_sem_give(g_learning_aging_master[lchip]->sw_aging_sem);
    }
    return CTC_E_NONE;
}

/**
 @brief Register user's callback function for CTC_EVENT_L2_SW_AGING event
*/
int32
sys_greatbelt_aging_set_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    g_learning_aging_master[lchip]->sw_aging_cb = cb;
    return CTC_E_NONE;
}

/**
 @brief ISR to handle CTC_INTR_GB_FUNC_IPE_FIB_LEARN_FIFO interrupt
*/
int32
sys_greatbelt_hw_learning_aging_isr(uint8 lchip, uint32 intr, void* p_data)
{
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_learning_aging_sync_init(uint8 lchip)
{
    sys_learning_aging_ds_t* p_master = g_learning_aging_master[lchip];
    int32 ret = CTC_E_NONE;
    uintptr lchip_tmp = lchip;
    char buffer[SAL_TASK_MAX_NAME_LEN] = {0};

    ret = sal_sem_create(&p_master->sw_learning_sem, 0);
    if (ret < 0)
    {
        goto error;
    }

    ret = sal_sem_create(&p_master->sw_aging_sem, 0);
    if (ret < 0)
    {
        goto error;
    }

    sal_sprintf(buffer, "ctcSwLrn-%d", lchip);
    ret = sal_task_create(&p_master->t_sw_learning, buffer,
                          SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_greatbelt_learning_thread, (void*)lchip_tmp);
    if (ret < 0)
    {
        goto error;
    }

    sal_sprintf(buffer, "ctcSwAge-%d", lchip);
    ret = sal_task_create(&p_master->t_sw_aging, buffer,
                          SAL_DEF_TASK_STACK_SIZE, SAL_TASK_PRIO_DEF, _sys_greatbelt_aging_thread, (void*)lchip_tmp);
    if (ret < 0)
    {
        goto error;
    }

    return CTC_E_NONE;

error:
    if (p_master->t_sw_learning)
    {
        sal_task_destroy(p_master->t_sw_learning);
        p_master->t_sw_learning = NULL;
    }

    if (p_master->t_sw_aging)
    {
        sal_task_destroy(p_master->t_sw_aging);
        p_master->t_sw_aging = NULL;
    }

    if (p_master->sw_learning_sem)
    {
        sal_sem_destroy(p_master->sw_learning_sem);
        p_master->sw_learning_sem = NULL;
    }

    if (p_master->sw_aging_sem)
    {
        sal_sem_destroy(p_master->sw_aging_sem);
        p_master->sw_aging_sem = NULL;
    }

    return ret;
}

/**
 @brief initialize learning and aging module
*/
int32
sys_greatbelt_learning_aging_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg)
{
    fib_learn_fifo_ctl_t fib_learn_fifo_ctl;
    uint32 cmd = 0;
    int32 ret = 0;

    if (!learning_mutex)
    {
        ret = sal_mutex_create(&learning_mutex);
        if (ret || !(learning_mutex))
        {
            return CTC_E_NO_RESOURCE;
        }
    }
    if (!aging_mutex)
    {
        ret = sal_mutex_create(&aging_mutex);
        if (ret || !(aging_mutex))
        {
            return CTC_E_NO_RESOURCE;
        }
    }

    CTC_ERROR_RETURN(_sys_greatbelt_learning_init(lchip, cfg));
    CTC_ERROR_RETURN(_sys_greatbelt_aging_init(lchip, cfg));
    CTC_ERROR_RETURN(_sys_greatbelt_learning_aging_sync_init(lchip));
    /* For HW Learning & Aging */
    cmd = DRV_IOR(FibLearnFifoCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_learn_fifo_ctl));
    fib_learn_fifo_ctl.learn_fifo_cpu_rd_thrd = 0;
    cmd = DRV_IOW(FibLearnFifoCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_learn_fifo_ctl));
    return CTC_E_NONE;
}

int32
sys_greatbelt_learning_aging_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == g_learning_aging_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_sem_give(g_learning_aging_master[lchip]->sw_learning_sem);
    sal_task_destroy(g_learning_aging_master[lchip]->t_sw_learning);

    sal_sem_give(g_learning_aging_master[lchip]->sw_aging_sem);
    sal_task_destroy(g_learning_aging_master[lchip]->t_sw_aging);

    sal_sem_destroy(g_learning_aging_master[lchip]->sw_learning_sem);
    sal_sem_destroy(g_learning_aging_master[lchip]->sw_aging_sem);

    mem_free(g_learning_aging_master[lchip]);

    return CTC_E_NONE;
}

