/**
   @file ctc_goldengate_learning_aging.c

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
#include "ctc_goldengate_interrupt.h"
#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_interrupt.h"
#include "sys_goldengate_learning_aging.h"
#include "sys_goldengate_dma.h"

#include "goldengate/include/drv_lib.h"

/****************************************************************************
 *
 * Defines and Macros
 *
 *****************************************************************************/
 /*#define SYS_LEARNING_CACHE_MAX_INDEX    16*/
#define SYS_LEARNING_CACHE_MAX_INDEX    32
#define SYS_TCAM_AGING_NUM              (21 * 1024)
#define MAC_HASH_LEVEL_NUM              5
#define FLW_HASH_LEVEL_NUM              4
#define IP_HASH_LEVEL_NUM              3
#define IGNORE_HW_CONTROL               0

#define NUM_16K                         (16 * 1024)
#define NUM_32K                         (32 * 1024)
#define NUM_64K                         (64 * 1024)


#define SYS_LEARNING_AGING_DBG_OUT(level, FMT, ...)                                           \
    {                                                                                         \
        CTC_DEBUG_OUT(l2, learning_aging, L2_LEARNING_AGING_SYS, level, FMT, ## __VA_ARGS__); \
    }


/**
   @brief struct type about aging info
 */
struct sys_learning_aging_master_s
{
    uint32                   aging_max_ptr;
    uint32                   aging_min_ptr;
    uint32                   mac_max_index[MAC_HASH_LEVEL_NUM];
    uint32                   flw_max_index[FLW_HASH_LEVEL_NUM];
    uint32                   ip_max_index[IP_HASH_LEVEL_NUM];
    uint8                    couple_mode;

    CTC_INTERRUPT_EVENT_FUNC sw_learning_cb;
    CTC_INTERRUPT_EVENT_FUNC sw_aging_cb;
 /*-    ctc_learning_aging_fn_t  sw_dma_cb;*/
    sys_learning_aging_fn_t  hw_dma_cb;
    sys_pending_aging_fn_t   pending_aging_cb;
    void                     * user_data;
};
typedef struct sys_learning_aging_master_s   sys_learning_aging_master_t;

/****************************************************************************
 *
 * Global and Declaration
 *
 *****************************************************************************/
static sys_learning_aging_master_t* pla_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_LEARNING_AGING_INIT_CHECK(lchip)             \
    do {                                \
        SYS_LCHIP_CHECK_ACTIVE(lchip);       \
        if (NULL == pla_master[lchip])  \
        { return CTC_E_NOT_INIT; }      \
    } while(0)


extern int32
sys_goldengate_l2_delete_hw_by_mac_fid(uint8 lchip, mac_addr_t mac, uint16 fid, uint8 mac_limit_en);

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/
#define ___LEARNING___
STATIC int32
_sys_goldengate_learning_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg)
{
    IpeLearningCacheValid_m learn_cache_vld;
    drv_work_platform_type_t   platform_type;
    uint32                     cmd       = 0;
    /*
       IpeLearningCacheValid.exceptionEn = 1:
       When alway cpu learning, set exceptionEn, then each needed learning pkt will send to cpu.
       When interrupt learning, set exceptionEn, cache full, then remained need learning pkt will send to cpu.
     */
    cmd = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    sal_memset(&learn_cache_vld, 0, sizeof(IpeLearningCacheValid_m));

    SetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld, 0);
    SetIpeLearningCacheValid(V, exceptionEn_f, &learn_cache_vld, 0);
    SetIpeLearningCacheValid(V, learningCacheIntThreshold_f, &learn_cache_vld, 0);
    SetIpeLearningCacheValid(V, macHashConflictLearningDisable_f, &learn_cache_vld, 1);
    SetIpeLearningCacheValid(V, dmaFifoEn_f, &learn_cache_vld, 1);   /* dma or fifo */
 /*    SetIpeLearningCacheValid(V, dmaFifoEn_f, &learn_cache_vld, 0);   // temp for testing */
    drv_goldengate_get_platform_type(&platform_type);
    if (platform_type == HW_PLATFORM)
    {
        SetIpeLearningCacheValid(V, learningEntryValid_f, &learn_cache_vld, 0xFFFF);
    }
    else
    {
        SetIpeLearningCacheValid(V, learningEntryValid_f, &learn_cache_vld, 0);
    }


    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &learn_cache_vld));


    CTC_ERROR_RETURN(sys_goldengate_dma_register_cb(lchip, SYS_DMA_CB_TYPE_LERNING,
        sys_goldengate_learning_aging_sync_data));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_learning_read_learning_cache(uint8 lchip,
                                             uint32 entry_vld_bitmap,
                                             ctc_learning_cache_t* l2_lc)
{
    IpeLearningCache_m learning_cache;
    uint32               cmd                = 0;
    uint8                index              = 0;
    uint8                entry_idx          = 0;
    uint32               source_port        = 0;
    uint32               is_global_src_port = 0;
    uint32               is_ether_oam       = 0;
    uint32               ether_oam_level    = 0;
    uint32               old_cvlan_id       = 0;
    uint32               old_svlan_id       = 0;
    uint32               new_cvlan_id       = 0;
    uint32               new_svlan_id       = 0;
    hw_mac_addr_t        hw_mac             = { 0 };
    uint32               vsi_id             = 0;


    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Learning] Read learning cache data: \n");

    /* check PARA */
    CTC_PTR_VALID_CHECK(l2_lc);

    l2_lc->entry_num = 0;
    if (0 == entry_vld_bitmap)
    {
        return CTC_E_NONE;
    }

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "entry_vld_bitmap = 0x%x\n", entry_vld_bitmap);

    sal_memset(&learning_cache, 0, sizeof(IpeLearningCache_m));
    cmd = DRV_IOR(IpeLearningCache_t, DRV_ENTRY_FLAG);

    for (index = 0, entry_idx = 0; index < SYS_LEARNING_CACHE_MAX_INDEX; index++)
    {
        /* Get info from the learning cache */
        if (CTC_IS_BIT_SET(entry_vld_bitmap, index))
        {
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &learning_cache));
            GetIpeLearningCache(A, isGlobalSrcPort_f, &learning_cache, &is_global_src_port);
            GetIpeLearningCache(A, sourcePort_f, &learning_cache, &source_port);
            GetIpeLearningCache(A, isEtherOam_f, &learning_cache, &is_ether_oam);
            GetIpeLearningCache(A, etherOamLevel_f, &learning_cache, &ether_oam_level);
            GetIpeLearningCache(A, oldCvlanId_f, &learning_cache, &old_cvlan_id);
            GetIpeLearningCache(A, oldSvlanId_f, &learning_cache, &old_svlan_id);
            GetIpeLearningCache(A, newCvlanId_f, &learning_cache, &new_cvlan_id);
            GetIpeLearningCache(A, newSvlanId_f, &learning_cache, &new_svlan_id);
            GetIpeLearningCache(A, macSa_f, &learning_cache, &hw_mac);
            GetIpeLearningCache(A, vsiId_f, &learning_cache, &vsi_id);

            l2_lc->learning_entry[entry_idx].cvlan_id           = old_cvlan_id;
            l2_lc->learning_entry[entry_idx].is_logic_port      = (!is_global_src_port);
            l2_lc->learning_entry[entry_idx].is_ether_oam       = is_ether_oam;
            l2_lc->learning_entry[entry_idx].svlan_id           = old_svlan_id;
            l2_lc->learning_entry[entry_idx].ether_oam_md_level = ether_oam_level;
            l2_lc->learning_entry[entry_idx].global_src_port    = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(source_port);
            l2_lc->learning_entry[entry_idx].logic_port         = source_port;
            l2_lc->learning_entry[entry_idx].mapped_vlan_id     = new_svlan_id;
            l2_lc->learning_entry[entry_idx].mac_sa_32to47      = hw_mac[1];
            l2_lc->learning_entry[entry_idx].mac_sa_0to31       = hw_mac[0];
            l2_lc->learning_entry[entry_idx].fid                = vsi_id;
            entry_idx++;
        }
    }

    l2_lc->entry_num = entry_idx;

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_learning_do_sw_learning_fifo(uint8 lchip)
{
    static drv_work_platform_type_t   platform_type;
    static IpeLearningCacheValid_m learn_cache_vld0;
    static IpeLearningCacheValid_m learn_cache_vld1;
    uint32                     cmd              = 0;
    uint8                      gchip            = 0;
    uint32                     value            = 0;
    uint32                     entry_vld_bitmap = 0;
    static ctc_learning_cache_t       cache;
    int32                      ret   = CTC_E_NONE;
    uint8                      count = 200;
    sal_systime_t              tv1, tv2;

    if (pla_master[lchip] == NULL)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(drv_goldengate_get_platform_type(&platform_type));
    sal_memset(&cache, 0, sizeof(cache));

    sal_gettime(&tv1);
    while (count)
    {
        /* 1. read valid bitmap */
        cmd = DRV_IOR(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld0));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &learn_cache_vld1));
        GetIpeLearningCacheValid(A, learningEntryValid_f, &learn_cache_vld0, &value);
        entry_vld_bitmap  = value ;
        GetIpeLearningCacheValid(A, learningEntryValid_f, &learn_cache_vld1, &value);
        entry_vld_bitmap |= value<<16 ;

        if (entry_vld_bitmap == 0)
        {
            sal_gettime(&tv2);
            SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, "[Learning] AgingPtr:time1:[0x%x:0x%x],time2:[0x%x:0x%x] time:[0x%x:0x%x]\n",
                                       tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec, tv2.tv_sec - tv1.tv_sec, tv2.tv_usec - tv1.tv_usec);

            break;
        }

        /* 2. read learning cache */
        CTC_ERROR_RETURN(_sys_goldengate_learning_read_learning_cache(lchip, entry_vld_bitmap, &cache));

        /* 3. call event callback function */
        if (pla_master[lchip]->sw_learning_cb)
        {
            ret = sys_goldengate_get_gchip_id(lchip, &gchip);
            if (0 == ret)
            {
                pla_master[lchip]->sw_learning_cb(gchip, &cache);
            }
        }

        /* 4. clear valid bitmap */
        if (platform_type != HW_PLATFORM)
        {
            if (!drv_goldengate_io_is_asic())
            {
                SetIpeLearningCacheValid(V, learningEntryValid_f, &learn_cache_vld0, 0);
                SetIpeLearningCacheValid(V, learningEntryValid_f, &learn_cache_vld1, 0);
            }
        }

        cmd = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld0));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &learn_cache_vld1));

        count--;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_learning_set_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action)
{
    IpeLearningCacheValid_m learn_cache_vld;
    uint32                     cmd       = 0;
    uint32                     field_val = 0;

    CTC_PTR_VALID_CHECK(p_learning_action);

    if (p_learning_action->action != CTC_LEARNING_ACTION_MAC_TABLE_FULL)
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
        SetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld, 1);
        SetIpeLearningCacheValid(V, exceptionEn_f, &learn_cache_vld, 1);
        break;

    case CTC_LEARNING_ACTION_CACHE_FULL_TO_CPU:
        SetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld, 0);
        SetIpeLearningCacheValid(V, exceptionEn_f, &learn_cache_vld, 1);
        if (p_learning_action->value >= SYS_LEARNING_CACHE_MAX_INDEX)
        {
            SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Learning threshold cfg exceeds the max value 0x10!\n");
            return CTC_E_LEARNING_AND_AGING_INVALID_LEARNING_THRESHOLD;
        }
        SetIpeLearningCacheValid(V, learningCacheIntThreshold_f, &learn_cache_vld, p_learning_action->value);
        break;

    case CTC_LEARNING_ACTION_CACHE_ONLY:
        SetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld, 0);
        SetIpeLearningCacheValid(V, exceptionEn_f, &learn_cache_vld, 0);
        if (p_learning_action->value >= SYS_LEARNING_CACHE_MAX_INDEX)
        {
            SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Learning threshold cfg exceeds the max value 0x10!\n");
            return CTC_E_LEARNING_AND_AGING_INVALID_LEARNING_THRESHOLD;
        }
        SetIpeLearningCacheValid(V, learningCacheIntThreshold_f, &learn_cache_vld, p_learning_action->value);
        break;

    case CTC_LEARNING_ACTION_DONLEARNING:
        SetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld, 1);
        SetIpeLearningCacheValid(V, exceptionEn_f, &learn_cache_vld, 0);
        break;

    case CTC_LEARNING_ACTION_MAC_TABLE_FULL:
        cmd       = DRV_IOW(IpeLearningCtl_t, IpeLearningCtl_macTableFull_f);
        field_val = (p_learning_action->value) ? 1 : 0;
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        return CTC_E_NONE;
    case CTC_LEARNING_ACTION_MAC_HASH_CONFLICT_LEARNING_DISABLE:
        SetIpeLearningCacheValid(V, macHashConflictLearningDisable_f, &learn_cache_vld, !!p_learning_action->value);
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd       = DRV_IOW(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &learn_cache_vld));


    return CTC_E_NONE;
}

int32
sys_goldengate_learning_get_action(uint8 lchip, ctc_learning_action_info_t* p_learning_action)
{
    uint32                     cmd = 0;
    IpeLearningCacheValid_m learn_cache_vld;

    CTC_PTR_VALID_CHECK(p_learning_action);

    cmd = DRV_IOR(IpeLearningCacheValid_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &learn_cache_vld));

    if (GetIpeLearningCacheValid(V, exceptionEn_f, &learn_cache_vld))
    {
        if (GetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld))
        {
            p_learning_action->action = CTC_LEARNING_ACTION_ALWAYS_CPU;
        }
        else
        {
            p_learning_action->action = CTC_LEARNING_ACTION_CACHE_FULL_TO_CPU;
            p_learning_action->value  = GetIpeLearningCacheValid(V, learningCacheIntThreshold_f, &learn_cache_vld);
        }
    }
    else
    {
        if (FALSE == GetIpeLearningCacheValid(V, alwaysCpuLearning_f, &learn_cache_vld))
        {
            p_learning_action->action = CTC_LEARNING_ACTION_CACHE_ONLY;
            p_learning_action->value  = GetIpeLearningCacheValid(V, learningCacheIntThreshold_f, &learn_cache_vld);
        }
        else
        {
            p_learning_action->action = CTC_LEARNING_ACTION_DONLEARNING;
        }
    }

    return CTC_E_NONE;
}


/**
   @brief ISR to handle CTC_INTR_GB_FUNC_IPE_LEARN_CACHE interrupt
 */
int32
sys_goldengate_learning_isr(uint8 lchip, uint32 intr, void* p_data)
{
    _sys_goldengate_learning_do_sw_learning_fifo(lchip);
    return CTC_E_NONE;
}

/**
   @brief Register user's callback function for CTC_EVENT_L2_SW_LEARNING event
 */
int32
sys_goldengate_learning_set_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    pla_master[lchip]->sw_learning_cb = cb;
    return CTC_E_NONE;
}


#define ___AGING___
STATIC int32
_sys_goldengate_aging_set_aging_interval(uint8 lchip, uint8 timer_idx, uint32 age_seconds)
{
    uint32 cmd             = 0;
    uint32 aging_interval  = 0;
    uint32 max_age_seconds = 0;
    uint32 min_age_seconds = 0;
    uint32 core_frequency  = 0;
    uint64 temp0           = 0;
    uint64 temp1           = 0;
    IpeAgingCtl_m aging_ctl;

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Aging] Set aging interval:aging_index: %d, age_seconds:%d\n", timer_idx, age_seconds);

 /*    core_frequency = sys_goldengate_get_core_freq(0) * 1000000 temp for testing;*/
    core_frequency = sys_goldengate_get_core_freq(lchip, 0) * 1000000 /DOWN_FRE_RATE;

    /* max intervale is 0xFFFFFFFF*/
    max_age_seconds = (0xFFFFFFFF / core_frequency) * (pla_master[lchip]->aging_max_ptr-pla_master[lchip]->aging_min_ptr);
    min_age_seconds = 1;
    if (age_seconds < min_age_seconds || age_seconds > max_age_seconds)
    {
        return CTC_E_AGING_INVALID_INTERVAL;
    }
    temp0 = (uint64)age_seconds * core_frequency;
    temp1 = pla_master[lchip]->aging_max_ptr-pla_master[lchip]->aging_min_ptr;
    aging_interval = temp0 / temp1;

    cmd = DRV_IOR(IpeAgingCtl_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0,cmd, &aging_ctl));

    switch(timer_idx)
    {
        case SYS_AGING_TIMER_INDEX_MAC:
            SetIpeAgingCtl(V, agingInterval0_f, &aging_ctl, aging_interval);
            break;
        case SYS_AGING_TIMER_INDEX_SERVICE:
            SetIpeAgingCtl(V, agingInterval1_f, &aging_ctl, aging_interval);
            break;
        case SYS_AGING_TIMER_INDEX_PENDING:
            SetIpeAgingCtl(V, agingInterval2_f, &aging_ctl, aging_interval);
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0,cmd, &aging_ctl));

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] Set aging aging_interval: %u max_age_seconds:%u \n", aging_interval, max_age_seconds);

    return CTC_E_NONE;
}

int32
sys_goldengate_learning_aging_set_hw_sync(uint8 lchip, uint8 b_sync)
{
    FibAccelerationCtl_m fib_acc_ctl;
    uint32                 cmd = 0;
    uint32 hw_learning = 0;
    uint32 dma_sync = 0;
    uint32 ignore_dma = 0;

    cmd = DRV_IOR(IpeLearningCtl_t, IpeLearningCtl_fastLearningEn_f);
    DRV_IOCTL(lchip, 0, cmd, &hw_learning);

    if (hw_learning)
    {
        dma_sync = b_sync;
        ignore_dma = (b_sync ? 0 : 1);
    }
    else
    {
        dma_sync = 0;
        ignore_dma = 0;
    }

    cmd = DRV_IOR(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acc_ctl));
    SetFibAccelerationCtl(V, dmaForFastLearning_f, &fib_acc_ctl, dma_sync);
    SetFibAccelerationCtl(V, dmaForFastAging_f, &fib_acc_ctl, dma_sync);
    SetFibAccelerationCtl(V, fastAgingIgnoreFifoFull_f, &fib_acc_ctl, ignore_dma);
    SetFibAccelerationCtl(V, fastLearningIgnoreFifoFull_f, &fib_acc_ctl, ignore_dma);
    cmd = DRV_IOW(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acc_ctl));

    return CTC_E_NONE;
}

 int32
_sys_goldengate_aging_set_register(uint8 lchip)
{
    uint32                 cmd = 0;
    uint8                  idx = 0;
    uint32                 couple_mode;
    uint32                 sum = 0;
    uint32                 en  = 0;
    FibAccelerationCtl_m fib_acc_ctl;
    FibHost1HashLookupCtl_m ip_hash_ctl;

    DRV_IF_ERROR_RETURN(drv_goldengate_get_dynamic_ram_couple_mode(&couple_mode));
    pla_master[lchip]->couple_mode = couple_mode;

    /* 1. mac hash dem */
    cmd = DRV_IOR(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acc_ctl));

    sum = 32;
    for (idx = 0; idx < 4; idx++)
    {
        if (IS_BIT_SET(DYNAMIC_BITMAP(DsFibHost0MacHashKey_t), idx))
        {
            sum                         += NUM_64K >> !couple_mode;
        }
        pla_master[lchip]->mac_max_index[idx] = sum;
    }

    if (IS_BIT_SET(DYNAMIC_BITMAP(DsFibHost0MacHashKey_t), idx))
    {
        sum                         += NUM_16K >> !couple_mode;
    }
    pla_master[lchip]->mac_max_index[idx] = sum;

    SetFibAccelerationCtl(V, demarcationIndex0_f, &fib_acc_ctl, pla_master[lchip]->mac_max_index[0]);
    SetFibAccelerationCtl(V, demarcationIndex1_f, &fib_acc_ctl, pla_master[lchip]->mac_max_index[1]);
    SetFibAccelerationCtl(V, demarcationIndex2_f, &fib_acc_ctl, pla_master[lchip]->mac_max_index[2]);
    SetFibAccelerationCtl(V, demarcationIndex3_f, &fib_acc_ctl, pla_master[lchip]->mac_max_index[3]);
    SetFibAccelerationCtl(V, demarcationIndex4_f, &fib_acc_ctl, pla_master[lchip]->mac_max_index[4]);
    SetFibAccelerationCtl(V, levelNum0_f, &fib_acc_ctl, 0);
    SetFibAccelerationCtl(V, levelNum1_f, &fib_acc_ctl, 1);
    SetFibAccelerationCtl(V, levelNum2_f, &fib_acc_ctl, 2);
    SetFibAccelerationCtl(V, levelNum3_f, &fib_acc_ctl, 3);
    SetFibAccelerationCtl(V, levelNum4_f, &fib_acc_ctl, 4);
    SetFibAccelerationCtl(V, host0HashSizeMode_f, &fib_acc_ctl, couple_mode);
    SetFibAccelerationCtl(V, macAgingTimerIndex0_f, &fib_acc_ctl, 1);   /*used for normal aging*/
    SetFibAccelerationCtl(V, macAgingTimerIndex1_f, &fib_acc_ctl, 3);   /*used for pending aging*/
    SetFibAccelerationCtl(V, macAgingTimerSelect_f, &fib_acc_ctl, 1);
    cmd = DRV_IOW(FibAccelerationCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_acc_ctl));

    /* 2. flow hash dem */
    if (TABLE_EXT_INFO_PTR(DsFlowL2HashKey_t))
    {
        sum = 32;
        for (idx = 0; idx < 4; idx++)
        {
            if (IS_BIT_SET(DYNAMIC_BITMAP(DsFlowL2HashKey_t), idx))
            {
                sum                         += NUM_64K >> !couple_mode;
                pla_master[lchip]->flw_max_index[idx] = sum;
            }
        }
    }

    /* 2. ip hash dem */
    sum = 32;
    idx = 0;
    cmd = DRV_IOR(FibHost1HashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ip_hash_ctl));
    en = GetFibHost1HashLookupCtl(V, fibHost1DaLevel0HashEn_f, &ip_hash_ctl);
    sum += en? (NUM_16K >> !couple_mode):0;
    pla_master[lchip]->ip_max_index[idx++] = sum;
    en = GetFibHost1HashLookupCtl(V, fibHost1DaLevel1HashEn_f, &ip_hash_ctl);
    sum += en? (NUM_16K >> !couple_mode):0;
    pla_master[lchip]->ip_max_index[idx++] = sum;
    en = GetFibHost1HashLookupCtl(V, fibHost1DaLevel2HashEn_f, &ip_hash_ctl);
    sum += en? (NUM_64K >> !couple_mode):0;
    pla_master[lchip]->ip_max_index[idx++] = sum;

    return CTC_E_NONE;
}


/*
    Aging timer0: for normal  fdb entry aging
    Aging timer1: for ipuc entry aging
    Aging timer2: for pending fdb entry aging
*/
STATIC int32
_sys_goldengate_aging_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg)
{
    IpeAgingCtl_m                ipe_aging_ctl;
    IpeAgingScanRangeCtl0_m     ipe_aging_range0;
    IpeAgingScanRangeCtl1_m     ipe_aging_range1;
    IpeAgingScanRangeCtl2_m     ipe_aging_range2;
    FibEngineLookupResultCtl_m fib_ctl;

    uint32                         cmd            = 0;

    CTC_ERROR_RETURN(_sys_goldengate_aging_set_register(lchip));

    /* IpeAgingCtl */
    cmd = DRV_IOR(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ctl));
    cmd = DRV_IOR(IpeAgingScanRangeCtl0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_range0));
    cmd = DRV_IOR(IpeAgingScanRangeCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_range1));
    cmd = DRV_IOR(IpeAgingScanRangeCtl2_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_range2));

    SetIpeAgingCtl(V, agingPtr0_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, agingPtr1_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, agingPtr2_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, scanEn0_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, scanEn1_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, scanEn2_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, agingIndexValid_f, &ipe_aging_ctl, 1);

    SetIpeAgingCtl(V, softwareReadClear_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, stopOnMaxPtr0_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, stopOnMaxPtr1_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, stopOnMaxPtr2_f, &ipe_aging_ctl, 0);
    SetIpeAgingCtl(V, fifoDepthThreshold_f, &ipe_aging_ctl, 1); /* check later*/
    SetIpeAgingCtl(V, tcamFullEn_f, &ipe_aging_ctl, 0);         /* if set, tcam size double*/
    SetIpeAgingCtl(V, useAgingFifo_f, &ipe_aging_ctl, 0);       /* fifo/dma */


    pla_master[lchip]->aging_max_ptr = 0x5145f; /* biggest:tcam 21k + host0 272k + host1 32k + cam 32*3 */
    pla_master[lchip]->aging_min_ptr = 0x5400;

    SetIpeAgingScanRangeCtl0(V, minPtr0_f, &ipe_aging_range0, pla_master[lchip]->aging_min_ptr);
    SetIpeAgingScanRangeCtl0(V, maxPtr0_f, &ipe_aging_range0, pla_master[lchip]->aging_max_ptr);

    SetIpeAgingScanRangeCtl1(V, minPtr1_f, &ipe_aging_range1, pla_master[lchip]->aging_min_ptr);
    SetIpeAgingScanRangeCtl1(V, maxPtr1_f, &ipe_aging_range1, pla_master[lchip]->aging_max_ptr);

    SetIpeAgingScanRangeCtl2(V, minPtr2_f, &ipe_aging_range2, pla_master[lchip]->aging_min_ptr);
    SetIpeAgingScanRangeCtl2(V, maxPtr2_f, &ipe_aging_range2, pla_master[lchip]->aging_max_ptr);

    cmd = DRV_IOW(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ctl));
    cmd = DRV_IOW(IpeAgingScanRangeCtl0_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_range0));
    cmd = DRV_IOW(IpeAgingScanRangeCtl1_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_range1));
    cmd = DRV_IOW(IpeAgingScanRangeCtl2_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_range2));

    /*aging interval  */
    CTC_ERROR_RETURN(_sys_goldengate_aging_set_aging_interval(lchip, SYS_AGING_TIMER_INDEX_MAC, 30));  /*30s*/
    CTC_ERROR_RETURN(_sys_goldengate_aging_set_aging_interval(lchip, SYS_AGING_TIMER_INDEX_SERVICE, 30));  /*30s*/
    CTC_ERROR_RETURN(_sys_goldengate_aging_set_aging_interval(lchip, SYS_AGING_TIMER_INDEX_PENDING, 5*60));  /*5*60s*/

    cmd = DRV_IOR(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ctl));
    SetIpeAgingCtl(V, scanEn2_f, &ipe_aging_ctl, 1);
    cmd = DRV_IOW(IpeAgingCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_aging_ctl));

    /* FibEngineLookupResultCtl */
    cmd = DRV_IOR(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_ctl));
    SetFibEngineLookupResultCtl(V, gIpv4McastLookupResultCtl_agingEn_f ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gIpv4NatSaLookupResultCtl_agingEn_f ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gIpv4UcastLookupResultCtl_agingEn_f ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gIpv6McastLookupResultCtl_agingEn_f ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gIpv6NatSaLookupResultCtl_agingEn_f ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gIpv6UcastLookupResultCtl_agingEn_f ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gMacDaLookupResultCtl_agingEn_f     ,&fib_ctl, 0);
    SetFibEngineLookupResultCtl(V, gMacIpv4LookupResultCtl_agingEn_f   ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gMacIpv6LookupResultCtl_agingEn_f   ,&fib_ctl, 1);
    SetFibEngineLookupResultCtl(V, gMacSaLookupResultCtl_agingEn_f     ,&fib_ctl, 1);
    cmd = DRV_IOW(FibEngineLookupResultCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &fib_ctl));

    sys_goldengate_aging_register_pending_cb(lchip, sys_goldengate_l2_remove_pending_fdb);

    return CTC_E_NONE;
}
int32
sys_goldengate_aging_index2ptr_out(uint8 lchip, uint32 key_index)
{
    uint32 aging_ptr = 0;
    sys_goldengate_aging_index2ptr(lchip, 1, key_index, &aging_ptr);
    return aging_ptr;
}

int32
sys_goldengate_aging_index2ptr(uint8 lchip, uint8 domain_type, uint32 key_index, uint32* aging_ptr)
{
    uint8 current_level = 0;

    switch (domain_type)
    {
    case SYS_AGING_DOMAIN_TCAM:
        *aging_ptr = key_index;
        break;

    case SYS_AGING_DOMAIN_MAC_HASH:
        if (key_index < 32)
        {
            *aging_ptr = key_index;
        }
        else
        {
            while (current_level < MAC_HASH_LEVEL_NUM)
            {
                if (key_index < pla_master[lchip]->mac_max_index[current_level])
                {
                    break;
                }
                current_level++;
            }

            key_index -= 32;
            if (pla_master[lchip]->couple_mode)
            {
                key_index &= 0xFFFF;
            }
            else
            {
                key_index &= 0x7FFF;
            }
            *aging_ptr = key_index + 32 + (NUM_64K * current_level);
        }

        break;

    case SYS_AGING_DOMAIN_FLOW_HASH:
        if (key_index < 32)
        {
            *aging_ptr = key_index + 32 + (NUM_64K * 4) + NUM_16K;
        }
        else
        {
            while (current_level < FLW_HASH_LEVEL_NUM)
            {
                if (key_index < pla_master[lchip]->flw_max_index[current_level])
                {
                    break;
                }
                current_level++;
            }

            key_index -= 32;
            if (pla_master[lchip]->couple_mode)
            {
                key_index &= 0xFFFF;
            }
            else
            {
                key_index &= 0x7FFF;
            }
            *aging_ptr = key_index + 32 + (NUM_64K * current_level);
        }

        break;

    case SYS_AGING_DOMAIN_IP_HASH:
        if (key_index < 32)
        {
            *aging_ptr = key_index + 32 + (NUM_64K * 4) + NUM_16K + 32;
        }
        else
        {
            while (current_level < IP_HASH_LEVEL_NUM)
            {
                if (key_index < pla_master[lchip]->ip_max_index[current_level])
                {
                    break;
                }
               current_level++;
            }
            key_index -= 32;

            if (current_level == 2)
            {
                key_index -= pla_master[lchip]->ip_max_index[current_level - 1];
                *aging_ptr = key_index + 32 ;
            }
            else
            {
                *aging_ptr = key_index + 32 + (NUM_64K * 4) + NUM_16K + 32 + 32;
            }

        }
        break;
    default:
        return CTC_E_INVALID_PARAM;
    }

    if (SYS_AGING_DOMAIN_TCAM != domain_type)
    {
        *aging_ptr += SYS_TCAM_AGING_NUM;
    }
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] index to AgingPtr:0x%x \n", *aging_ptr);

    return CTC_E_NONE;
}

/*
 +-----------------------+
 |   TCAM       (21K)    |
 |-----------------------|0
 |   MAC CAM    (32)     |32-1
 |-----------------------|32
 |   MAC0/FLOW0/IP3(64K) |32+32K-1
 |-----------------------|32+64K
 |   MAC1/FLOW1 (64K)    |32+64K+32K-1
 |-----------------------|32+64K+64K
 |   MAC2/FLOW2 (64K)    |32+64K+64K+32K-1
 |-----------------------|32+64K+64K+64K
 |   MAC3/FLOW3 (64K)    |32+64K+64K+64K+32K-1
 |-----------------------|32+64K+64K+64K+64K
 |   MAC4       (16K)    |32+64K+64K+64K+64K+8K-1
 |-----------------------|
 |   Flow Cam   (32)     |
 |-----------------------|
 |   IP Cam     (32)     |
 |-----------------------|
 |   IP 0       (16K)    |
 |-----------------------|
 |   IP 1       (16K)    |
 +-----------------------+
*/


int32
sys_goldengate_aging_ptr2index(uint8 lchip, uint32 aging_ptr, uint8* domain_type, uint32* key_index)
{
    uint32 mac_bitmap = 0;
    uint32 flw_bitmap = 0;
    uint32 ip_bitmap = 0;
    uint32 entry_num = 0;

    sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost0MacHashKey_t, &entry_num);
    if (0 != entry_num)
    {
        mac_bitmap = DYNAMIC_BITMAP(DsFibHost0MacHashKey_t);
    }

    entry_num = 0;
    sys_goldengate_ftm_query_table_entry_num(lchip, DsFlowL2HashKey_t, &entry_num);
    if (0 != entry_num)
    {
        flw_bitmap = DYNAMIC_BITMAP(DsFlowL2HashKey_t);
    }

    entry_num = 0;
    sys_goldengate_ftm_query_table_entry_num(lchip, DsFibHost1Ipv4McastHashKey_t, &entry_num);
    if (0 != entry_num)
    {
        ip_bitmap = DYNAMIC_BITMAP(DsFibHost1Ipv4McastHashKey_t);
    }


    CTC_PTR_VALID_CHECK(domain_type);
    CTC_PTR_VALID_CHECK(key_index);

    if (aging_ptr < SYS_TCAM_AGING_NUM)  /*0*/
    {
        *domain_type = SYS_AGING_DOMAIN_TCAM;
        *key_index   = aging_ptr;
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32))  /*21504*/
    {
        *domain_type = SYS_AGING_DOMAIN_MAC_HASH;
        *key_index   = aging_ptr - SYS_TCAM_AGING_NUM;
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K)) /* 21536*/
    {
        if (IS_BIT_SET(mac_bitmap, 0))
        {
            *domain_type = SYS_AGING_DOMAIN_MAC_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32) + 32;
        }
        else if (IS_BIT_SET(flw_bitmap, 0))
        {
            *domain_type = SYS_AGING_DOMAIN_FLOW_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32) + 32;
        }
        else if (IS_BIT_SET(ip_bitmap, 0))
        {
            *domain_type = SYS_AGING_DOMAIN_FLOW_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32) + 32 + pla_master[lchip]->ip_max_index[1];;
        }

    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 2)) /* 87072*/
    {
        if (IS_BIT_SET(mac_bitmap, 1))
        {
            *domain_type = SYS_AGING_DOMAIN_MAC_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K) + pla_master[lchip]->mac_max_index[0];
        }
        else if (IS_BIT_SET(flw_bitmap, 1))
        {
            *domain_type = SYS_AGING_DOMAIN_FLOW_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K) + pla_master[lchip]->flw_max_index[0];
        }
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 3)) /*152608*/
    {
        if (IS_BIT_SET(mac_bitmap, 2))
        {
            *domain_type = SYS_AGING_DOMAIN_MAC_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 2) + pla_master[lchip]->mac_max_index[1];
        }
        else if (IS_BIT_SET(flw_bitmap, 2))
        {
            *domain_type = SYS_AGING_DOMAIN_FLOW_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 2) + pla_master[lchip]->flw_max_index[1];
        }
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4)) /*218144*/
    {
        if (IS_BIT_SET(mac_bitmap, 3))
        {
            *domain_type = SYS_AGING_DOMAIN_MAC_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 3) + pla_master[lchip]->mac_max_index[2];
        }
        else if (IS_BIT_SET(flw_bitmap, 3))
        {
            *domain_type = SYS_AGING_DOMAIN_FLOW_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 3) + pla_master[lchip]->flw_max_index[2];
        }
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K)) /*283680*/
    {
        if (IS_BIT_SET(mac_bitmap, 4))
        {
            *domain_type = SYS_AGING_DOMAIN_MAC_HASH;
            *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4) + pla_master[lchip]->mac_max_index[3];
        }
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K + 32)) /*300064*/
    {
        *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K);
        *domain_type = SYS_AGING_DOMAIN_FLOW_HASH;
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K + 32 + 32)) /*300096*/
    {
        *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K + 32);
        *domain_type = SYS_AGING_DOMAIN_IP_HASH;
    }
    else if (aging_ptr < (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K + 32 + 32 + NUM_32K)) /*300128*/
    {
        *key_index   = aging_ptr - (SYS_TCAM_AGING_NUM + 32 + NUM_64K * 4 + NUM_16K + 32);
        *domain_type = SYS_AGING_DOMAIN_IP_HASH;
    }
    else /*332896*/
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_aging_read_aging_fifo(uint8 lchip, ctc_aging_fifo_info_t* fifo_info)
{
    drv_work_platform_type_t platform_type;


    uint32 depth;
    uint32 cmd         = 0;
    uint32 key_index   = 0;
    uint32 aging_ptr   = 0;
    int32  ret         = 0;
    uint8  domain_type = 0;
    uint8  fifo_idx    = 0;
    uint8  index       = 0;

    CTC_PTR_VALID_CHECK(fifo_info);

    cmd = DRV_IOR(DsAgingSatuFifoDepth_t, DsAgingSatuFifoDepth_dsAgingSatuFifoFifoDepth_f);
    ret = DRV_IOCTL(lchip, 0, cmd, &depth);


    /* Get aging index from aging FIFO according CTC_AGING_FIFO_DEPTH*/
    for (index = 0; index < depth; index++)
    {
        cmd = DRV_IOR(DsAgingSatuPtr_t, DsAgingSatuPtr_agingSatuPtr_f);
        ret = DRV_IOCTL(lchip, 0, cmd, &aging_ptr);
        if (ret < 0)
        {
            continue;
        }

        ret = sys_goldengate_aging_ptr2index(lchip, aging_ptr, &domain_type, &key_index);
        if (ret < 0)
        {
            continue;
        }

        fifo_info->aging_index_array[fifo_idx] = key_index;
        if (SYS_AGING_DOMAIN_TCAM == domain_type ||
            SYS_AGING_DOMAIN_MAC_HASH == domain_type)
        {
            fifo_info->tbl_type[fifo_idx] = CTC_AGING_TBL_MAC;
        }

        fifo_idx++;
    }

    fifo_info->fifo_idx_num = fifo_idx;
    drv_goldengate_get_platform_type(&platform_type);
    if (platform_type != HW_PLATFORM)
    {
        depth = 0;
        cmd   = DRV_IOW(DsAgingSatuFifoDepth_t, DsAgingSatuFifoDepth_dsAgingSatuFifoFifoDepth_f);
        ret   = DRV_IOCTL(lchip, 0, cmd, &depth);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_aging_ptr2index_out(uint8 lchip, uint32 ptr)
{
    uint32 key_index = 0;
    uint8  type = 0;
    sys_goldengate_aging_ptr2index(lchip, ptr, &type, &key_index);
    return key_index;
}

STATIC int32
_sys_goldengate_aging_do_sw_aging(uint8 lchip)
{
    static ctc_aging_fifo_info_t aging_fifo;
    uint8                 gchip = 0;
    int32                 ret   = CTC_E_NONE;
    uint8                 count = 100;
    sal_systime_t         tv1, tv2;
    sal_memset(&aging_fifo, 0, sizeof(aging_fifo));
    sal_gettime(&tv1);

    if (NULL == pla_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    while (count)
    {
        /* 1. read aging FIFO */
        _sys_goldengate_aging_read_aging_fifo(lchip, &aging_fifo);

        if (aging_fifo.fifo_idx_num == 0)
        {
            break;
        }

        /* 2. call event callback function */
        if (pla_master[lchip]->sw_aging_cb)
        {
            ret = sys_goldengate_get_gchip_id(lchip, &gchip);
            if (0 == ret)
            {
                pla_master[lchip]->sw_aging_cb(gchip, &aging_fifo);
            }
        }

        count--;
    }

    sal_gettime(&tv2);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] AgingPtr:time1:[0x%x:0x%x],[0x%x:0x%x] \n",
                               tv1.tv_sec, tv1.tv_usec, tv2.tv_sec, tv2.tv_usec);

    return CTC_E_NONE;
}



/**
   @brief This function is to set the aging properties
 */
int32
sys_goldengate_aging_set_property(uint8 lchip, ctc_aging_table_type_t tbl_type, ctc_aging_prop_t aging_prop, uint32 value)
{
    uint32 cmd         = 0;
    uint16 field       = 0;
    uint8  timer_index = 0;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "[Aging] Set aging property:tbl_type %d, aging_prop:%d, value:%d\n", tbl_type, aging_prop, value);

    if (tbl_type == CTC_AGING_TBL_MAC)
    {
        timer_index = SYS_AGING_TIMER_INDEX_MAC;
    }

    switch (aging_prop)
    {
    case CTC_AGING_PROP_FIFO_THRESHOLD:
        field = IpeAgingCtl_fifoDepthThreshold_f;
        CTC_MAX_VALUE_CHECK(value, 0xF);
        break;

    case CTC_AGING_PROP_INTERVAL:

        return(_sys_goldengate_aging_set_aging_interval(lchip, timer_index, value));

    case CTC_AGING_PROP_STOP_SCAN_TIMER_EXPIRED:
        field = IpeAgingCtl_stopOnMaxPtr0_f;
        value = !!value;
        break;

    case CTC_AGING_PROP_AGING_SCAN_EN:
        field = IpeAgingCtl_scanEn0_f + timer_index - 1;
        value = !!value;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    cmd       = DRV_IOW(IpeAgingCtl_t, field);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));


    return CTC_E_NONE;
}

/**
   @brief This function is to get the aging properties
 */
int32
sys_goldengate_aging_get_property(uint8 lchip, ctc_aging_table_type_t tbl_type, ctc_aging_prop_t aging_prop, uint32* value)
{
    uint32 cmd         = 0;
    uint16 field       = 0;
    uint8  timer_index = 0;
    uint32 core_frequency = 0;
    uint64 temp_val = 0;
    uint64 temp = 0;

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
        field = IpeAgingCtl_fifoDepthThreshold_f;
        break;

    case CTC_AGING_PROP_INTERVAL:
        field = IpeAgingCtl_agingInterval0_f + timer_index;
        break;

    case CTC_AGING_PROP_STOP_SCAN_TIMER_EXPIRED:
        field = IpeAgingCtl_stopOnMaxPtr0_f;
        break;

    case CTC_AGING_PROP_AGING_SCAN_EN:
        field = IpeAgingCtl_scanEn0_f + timer_index;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }


    cmd       = DRV_IOR(IpeAgingCtl_t, field);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, value));

    if (aging_prop == CTC_AGING_PROP_INTERVAL)
    {
        core_frequency = sys_goldengate_get_core_freq(lchip, 0) * 1000000/DOWN_FRE_RATE;
        temp_val = (*value);
        temp = (temp_val+1)*(pla_master[lchip]->aging_max_ptr-pla_master[lchip]->aging_min_ptr);
        *value = (temp-1)/core_frequency;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_aging_output_aging_status_by_ptr(uint8 lchip, uint32 domain_type, uint32 aging_ptr, uint8 *status)
{
    DsAgingStatus_m ds_aging_status;
    uint32            status_tbl = 0;
    uint32            status_ptr = 0;
    uint32            aging_status = 0;

    status_ptr = (SYS_AGING_DOMAIN_TCAM == domain_type) ? aging_ptr : (aging_ptr - SYS_TCAM_AGING_NUM);
    status_tbl = (SYS_AGING_DOMAIN_TCAM == domain_type) ? DsAgingStatusTcam_t : DsAgingStatusFib_t;

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, status_ptr >> 3,
                               DRV_IOR(status_tbl, DRV_ENTRY_FLAG), &ds_aging_status));

    aging_status = DRV_GET_FIELD_V(status_tbl, DsAgingStatus_array_0_agingStatus_f + (status_ptr & 7), &ds_aging_status);

    *status = (uint8)aging_status;
    return CTC_E_NONE;
}

int32
sys_goldengate_aging_set_aging_status_by_ptr(uint8 lchip, uint32 domain_type, uint32 aging_ptr, uint8 aging_status)
{

    DsAgingStatus_m ds_aging_status;
    uint32            status_tbl = 0;
    uint32            status_ptr = 0;

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    status_ptr = (SYS_AGING_DOMAIN_TCAM == domain_type) ? aging_ptr : (aging_ptr - SYS_TCAM_AGING_NUM);
    status_tbl = (SYS_AGING_DOMAIN_TCAM == domain_type) ? DsAgingStatusTcam_t : DsAgingStatusFib_t;


    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] Set aging status-- AgingPtr:0x%x \n", aging_ptr);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, status_ptr >> 3,
                               DRV_IOR(status_tbl, DRV_ENTRY_FLAG), &ds_aging_status));

    if (aging_status)
    {
        /* always 0. leave the control to global. */
        DRV_SET_FIELD_V(status_tbl, DsAgingStatus_array_0_agingStatus_f + (status_ptr & 7), &ds_aging_status, 1);
    }
    else
    {
        DRV_SET_FIELD_V(status_tbl, DsAgingStatus_array_0_agingStatus_f + (status_ptr & 7), &ds_aging_status, 0);
    }

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, status_ptr >> 3,
                               DRV_IOW(status_tbl, DRV_ENTRY_FLAG), &ds_aging_status));

    return CTC_E_NONE;
}


int32
sys_goldengate_aging_set_aging_status(uint8 lchip, sys_aging_domain_type_t domain_type, uint32 key_index, uint8 hit)
{
    uint32            aging_ptr  = 0;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] key_index:%d, domain_type:%d, hit:%d\n",
                               key_index, domain_type, hit);

    CTC_ERROR_RETURN(sys_goldengate_aging_index2ptr(lchip, domain_type, key_index, &aging_ptr));
    CTC_ERROR_RETURN(sys_goldengate_aging_set_aging_status_by_ptr(lchip, domain_type, aging_ptr, hit));

    return CTC_E_NONE;
}

int32
sys_goldengate_aging_get_aging_status(uint8 lchip, sys_aging_domain_type_t domain_type, uint32 key_index, uint8* hit)
{
    uint32  aging_ptr  = 0;
    uint8   status = 0;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_aging_index2ptr(lchip, domain_type, key_index, &aging_ptr));
    CTC_ERROR_RETURN(sys_goldengate_aging_output_aging_status_by_ptr(lchip, domain_type, aging_ptr, &status));

    if (NULL != hit)
    {
        *hit = status;
    }

    SYS_LEARNING_AGING_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "[Aging] key_index:%d, domain_type:%d, hit:%d\n",
                               key_index, domain_type, *hit);

    return CTC_E_NONE;
}



int32
sys_goldengate_aging_set_aging_timer(uint8 lchip, uint32 key_index, uint8 timer)
{
    uint32            aging_ptr  = 0;
    DsAging_m        ds_aging;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_aging_index2ptr(lchip, SYS_AGING_DOMAIN_MAC_HASH, key_index, &aging_ptr));

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aging_ptr >> 3,
                               DRV_IOR(DsAging_t, DRV_ENTRY_FLAG), &ds_aging));
    DRV_SET_FIELD_V(DsAging_t, DsAging_array_0_agingIndex_f + (aging_ptr & 7) * 2, &ds_aging, timer);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aging_ptr >> 3,
                               DRV_IOW(DsAging_t, DRV_ENTRY_FLAG), &ds_aging));

    return CTC_E_NONE;
}

int32
sys_goldengate_aging_get_aging_timer(uint8 lchip, uint32 key_index, uint8* p_timer)
{
    uint32            aging_ptr  = 0;
    DsAging_m        ds_aging;

    SYS_LEARNING_AGING_INIT_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_aging_index2ptr(lchip, SYS_AGING_DOMAIN_MAC_HASH, key_index, &aging_ptr));

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, aging_ptr >> 3,
                               DRV_IOR(DsAging_t, DRV_ENTRY_FLAG), &ds_aging));
    *p_timer = DRV_GET_FIELD_V(DsAging_t, DsAging_array_0_agingIndex_f + (aging_ptr & 7) * 2, &ds_aging);

    return CTC_E_NONE;
}

/**
   @brief ISR to handle CTC_INTR_GB_FUNC_IPE_AGING_FIFO interrupt
 */
int32
sys_goldengate_aging_isr(uint8 lchip, uint32 intr, void* p_data)
{
    _sys_goldengate_aging_do_sw_aging(lchip);
    return CTC_E_NONE;
}

/**
   @brief Register user's callback function for CTC_EVENT_L2_SW_AGING event
 */
int32
sys_goldengate_aging_set_event_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    pla_master[lchip]->sw_aging_cb = cb;
    return CTC_E_NONE;
}

#define ___LEARNING_AGING___

/* this callback is for sample code. specially for testing HW learning send DMA. */
int32
sys_goldengate_learning_aging_register_cb2(uint8 lchip, sys_learning_aging_fn_t cb)
{
    pla_master[lchip]->hw_dma_cb    = cb;

    return CTC_E_NONE;
}

/* this callback is for sample code. specially for testing pending aging send DMA. */
int32
sys_goldengate_aging_register_pending_cb(uint8 lchip, sys_pending_aging_fn_t cb)
{
    pla_master[lchip]->pending_aging_cb = cb;

    return CTC_E_NONE;
}

#if 0
int32
sys_goldengate_learning_aging_register_cb(uint8 lchip, ctc_learning_aging_fn_t cb, void* user_data)
{
    pla_master[lchip]->sw_dma_cb    = cb;
    pla_master[lchip]->user_data = user_data;

    return CTC_E_NONE;
}
#endif

int32
sys_goldengate_learning_aging_get_cb(uint8 lchip, void **cb, void** user_data, uint8 type)
{
    *user_data = pla_master[lchip]->user_data;
    if (type)
    {
        *cb = pla_master[lchip]->sw_learning_cb;
    }
    else
    {
        *cb = pla_master[lchip]->sw_aging_cb;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_learning_aging_init(uint8 lchip, ctc_learn_aging_global_cfg_t* cfg)
{
    if (pla_master[lchip])
    {
        return CTC_E_NONE;
    }

    MALLOC_ZERO(MEM_FDB_MODULE, pla_master[lchip], sizeof(sys_learning_aging_master_t));
    if (!pla_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }
    CTC_ERROR_RETURN(_sys_goldengate_learning_init(lchip, cfg));
    CTC_ERROR_RETURN(_sys_goldengate_aging_init(lchip, cfg));

    return CTC_E_NONE;
}

int32
sys_goldengate_learning_aging_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == pla_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(pla_master[lchip]);

    return CTC_E_NONE;
}

#define __SYNC_DMA_INFO__
STATIC int32
_sys_goldengate_learning_aging_decode_leaning_data(uint8 lchip, void* p_info, ctc_learning_cache_entry_t* p_entry)
{
    DmaFibLearnFifo_m* p_learning = (DmaFibLearnFifo_m*)p_info;
    hw_mac_addr_t             mac_sa   = { 0 };

    GetDmaFibLearnFifo(A, macSa_f, p_learning, mac_sa);
    SYS_GOLDENGATE_SET_USER_MAC(p_entry->mac, mac_sa);
    p_entry->fid                = GetDmaFibLearnFifo(V, vsiId_f, p_learning);

    if (!GetDmaFibLearnFifo(V, isGlobalSrcPort_f, p_learning))
    {
        p_entry->logic_port = GetDmaFibLearnFifo(V, learningSourcePort_f, p_learning);
        p_entry->is_logic_port = 1;
    }
    else
    {
        p_entry->is_logic_port = 0;
        p_entry->global_src_port = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDmaFibLearnFifo(V, learningSourcePort_f, p_learning));
    }

    p_entry->cvlan_id = GetDmaFibLearnFifo(V, uShare_gLearning_oldCvlanId_f, p_learning);
    p_entry->svlan_id = GetDmaFibLearnFifo(V, uShare_gLearning_oldSvlanId_f, p_learning);

    p_entry->ether_oam_md_level = GetDmaFibLearnFifo(V, uShare_gLearning_etherOamLevel_f, p_learning);
    p_entry->is_ether_oam       = GetDmaFibLearnFifo(V, uShare_gLearning_isEtherOam_f, p_learning);

    p_entry->mac_sa_0to31 = (p_entry->mac[5] | (p_entry->mac[4] << 8) |
        (p_entry->mac[3] << 16) | (p_entry->mac[2] << 24));
    p_entry->mac_sa_32to47 = (p_entry->mac[1] | (p_entry->mac[0] << 8));
    p_entry->is_hw_sync =  GetDmaFibLearnFifo(V, fastLearningEn_f, p_learning);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_learning_aging_decode_aging_data(uint8 lchip, void* p_info, ctc_aging_info_entry_t* p_entry, uint8* is_ip)
{
    DmaFibLearnFifo_m* p_aging = (DmaFibLearnFifo_m*)p_info;
    uint8   hash_type = 0;

    hw_mac_addr_t             mac_sa   = { 0 };
    uint32  aging_ptr = 0;
    uint8   domain_type = 0;
    drv_fib_acc_in_t fib_acc_in;
    drv_fib_acc_out_t fib_acc_out;
    uint32  key_index = 0;
    uint32  temp = 0;

    hash_type = GetDmaFibLearnFifo(V, uShare_gAging_hashType_f, p_aging);

    if (hash_type == FIBHOST0PRIMARYHASHTYPE_MAC)
    {
        /*1.1 mac aging */
        GetDmaFibLearnFifo(A, macSa_f, p_aging, mac_sa);
        SYS_GOLDENGATE_SET_USER_MAC(p_entry->mac, mac_sa);
        p_entry->fid                = GetDmaFibLearnFifo(V, vsiId_f, p_aging);
        p_entry->is_hw_sync        = GetDmaFibLearnFifo(V, uShare_gAging_hwAging_f, p_aging);
        p_entry->aging_type = CTC_AGING_TYPE_MAC;
        *is_ip = 0;
    }
    else if(hash_type == FIBHOST0PRIMARYHASHTYPE_IPV4)
    {
        /*1.2 Ipv4 aging */
        aging_ptr = GetDmaFibLearnFifo(V, uShare_gAging_agingPtr_f, p_aging);
        CTC_ERROR_RETURN(sys_goldengate_aging_ptr2index(lchip, aging_ptr, &domain_type, &key_index));
        if (domain_type != SYS_AGING_DOMAIN_MAC_HASH)
        {
             return CTC_E_INVALID_PARAM;
        }

        fib_acc_in.rw.tbl_id = DsFibHost0Ipv4HashKey_t;
        fib_acc_in.rw.key_index = key_index;
        CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out));

        p_entry->aging_type = CTC_AGING_TYPE_IPV4_UCAST;
        p_entry->ip_da.ipv4 = GetDsFibHost0Ipv4HashKey(V, ipDa_f, &(fib_acc_out.read.data));
        p_entry->vrf_id = GetDsFibHost0Ipv4HashKey(V, vrfId_f, &(fib_acc_out.read.data));
        p_entry->masklen = 32;
        *is_ip = 1;
    }
    else if(hash_type == FIBHOST0PRIMARYHASHTYPE_IPV6UCAST)
    {
        /*1.3 Ipv6 aging */
        aging_ptr = GetDmaFibLearnFifo(V, uShare_gAging_agingPtr_f, p_aging);
        CTC_ERROR_RETURN(sys_goldengate_aging_ptr2index(lchip, aging_ptr, &domain_type, &key_index));
        if (domain_type != SYS_AGING_DOMAIN_MAC_HASH)
        {
            return CTC_E_INVALID_PARAM;
        }

        fib_acc_in.rw.tbl_id = DsFibHost0Ipv6UcastHashKey_t;
        fib_acc_in.rw.key_index = key_index;
        CTC_ERROR_RETURN(drv_goldengate_fib_acc(lchip, DRV_FIB_ACC_READ_FIB0_BY_IDX, &fib_acc_in, &fib_acc_out));

        p_entry->aging_type = CTC_AGING_TYPE_IPV6_UCAST;
        GetDsFibHost0Ipv6UcastHashKey(A, ipDa_f, &(fib_acc_out.read.data), &(p_entry->ip_da.ipv6));
        p_entry->vrf_id = GetDsFibHost0Ipv6UcastHashKey(V, vrfId_f, &(fib_acc_out.read.data));
        p_entry->masklen = 128;

        temp = p_entry->ip_da.ipv6[0];
        p_entry->ip_da.ipv6[0] = p_entry->ip_da.ipv6[3];
        p_entry->ip_da.ipv6[3] = temp;
        temp = p_entry->ip_da.ipv6[1];
        p_entry->ip_da.ipv6[1] = p_entry->ip_da.ipv6[2];
        p_entry->ip_da.ipv6[2] = temp;
        *is_ip = 1;
    }
    else
    {
        /*Not support*/
        return CTC_E_FEATURE_NOT_SUPPORT;
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_learning_aging_sync_data(uint8 lchip, void* p_info)
{
    uint32                     index    = 0;
    uint32                     i        = 0;
    sys_dma_learning_info_t   * p_dma  = NULL;
    DmaFibLearnFifo_m      * p_fifo = NULL;
    static ctc_l2_addr_t l2_addr;
    static ctc_learning_cache_t ctc_learning;
    static ctc_aging_fifo_info_t ctc_aging;
    hw_mac_addr_t             mac_sa   = { 0 };
    uint8   is_aging = 0;
    uint8 is_ip_aging = 0;
    sys_dma_info_t* p_dma_info = NULL;
    uint8 gchip = 0;
    uint8 leaning_cnt = 0;
    uint8 aging_cnt = 0;
    uint8 is_pending = 0;
    uint32 ad_idx = 0;

    CTC_PTR_VALID_CHECK(p_info);
    CTC_PTR_VALID_CHECK(pla_master[lchip]);

    ctc_learning.sync_mode = 1;
    ctc_aging.sync_mode = 1;

    p_dma_info = (sys_dma_info_t*)p_info;

    p_dma = (sys_dma_learning_info_t *) p_dma_info->p_data;
    for (i = 0; i < p_dma_info->entry_num; i++)
    {
        if (i >= SYS_DMA_LEARN_AGING_SYNC_CNT)
        {
            return CTC_E_EXCEED_MAX_SIZE;
        }

        is_pending = 0;
        p_fifo   = &p_dma[i].learn_info;
        is_aging  = !GetDmaFibLearnFifo(V, isLearning_f, p_fifo);
        if (!is_aging)
        {
            _sys_goldengate_learning_aging_decode_leaning_data(lchip, p_fifo, &(ctc_learning.learning_entry[leaning_cnt]));
            leaning_cnt++;
        }
        else
        {
            is_pending = ((GetDmaFibLearnFifo(V, uShare_gAging_hashType_f, p_fifo) == FIBHOST0PRIMARYHASHTYPE_MAC) \
                 && GetDmaFibLearnFifo(V, isGlobalSrcPort_f, p_fifo));

            if (is_pending)
            {
                /*for pending aging , sdk process direct , do not sync to system*/
                l2_addr.fid = GetDmaFibLearnFifo(V, vsiId_f, p_fifo);
                ad_idx = GetDmaFibLearnFifo(V, uShare_gAging_dsAdIndex_f, p_fifo);
                GetDmaFibLearnFifo(A, macSa_f, p_fifo, mac_sa);
                SYS_GOLDENGATE_SET_USER_MAC(l2_addr.mac, mac_sa);
                if (pla_master[lchip]->pending_aging_cb)
                {
                    pla_master[lchip]->pending_aging_cb(lchip, l2_addr.mac, l2_addr.fid, ad_idx);
                }

                continue;
            }
            _sys_goldengate_learning_aging_decode_aging_data(lchip, p_fifo, &(ctc_aging.aging_entry[aging_cnt]), &is_ip_aging);
            aging_cnt++;
        }
        index ++;
    }

    if (index == 0)
    {
        return CTC_E_NONE;
    }

    ctc_learning.entry_num = leaning_cnt;
    ctc_aging.fifo_idx_num = aging_cnt;

    /*ip aging is needed even if hw learning mode */
    CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));

    /* call event callback function */
    if (leaning_cnt)
    {
        if (pla_master[lchip]->sw_learning_cb)
        {
            pla_master[lchip]->sw_learning_cb(gchip, &ctc_learning);
        }
    }

    if (aging_cnt)
    {
         if (pla_master[lchip]->sw_aging_cb)
        {
            pla_master[lchip]->sw_aging_cb(gchip, &ctc_aging);
        }
    }

    return CTC_E_NONE;
}


