/**
 @file sys_goldengate_efd.c

 @date 2014-10-28

 @version v3.0


*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_efd.h"
#include "sys_goldengate_nexthop_api.h"
#include "sys_goldengate_queue_enq.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"


/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_EFD_INIT_CHECK(lchip)                           \
do {                                                        \
    SYS_LCHIP_CHECK_ACTIVE(lchip);                          \
    if (NULL == p_gg_efd_master[lchip])                     \
    {                                                       \
        return CTC_E_NOT_INIT;                              \
    }                                                       \
} while(0)

#define SYS_EFD_CREATE_LOCK(lchip)                          \
    do                                                      \
    {                                                       \
        sal_mutex_create(&p_gg_efd_master[lchip]->mutex);   \
        if (NULL == p_gg_efd_master[lchip]->mutex)          \
        {                                                   \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);      \
        }                                                   \
    } while (0)

#define SYS_EFD_LOCK(lchip) \
    sal_mutex_lock(p_gg_efd_master[lchip]->mutex)

#define SYS_EFD_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_efd_master[lchip]->mutex)

struct sys_efd_master_s
{
    ctc_efd_fn_t func;
    void* userdata;
    sal_mutex_t* mutex;
};
typedef struct sys_efd_master_s sys_efd_master_t;

sys_efd_master_t* p_gg_efd_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_EFD_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(efd, efd, EFD_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

STATIC int32
_sys_goldengate_efd_get_granu(uint8 lchip, uint32* granu)
{
    uint32 field_val = 0;

    CTC_ERROR_RETURN(sys_goldengate_efd_get_global_ctl(lchip, CTC_EFD_GLOBAL_DETECT_GRANU, &field_val));

    if (0 == field_val)
    {
        *granu = 16;
    }
    else if (1 == field_val)
    {
        *granu = 8;
    }
    else if (2 == field_val)
    {
        *granu = 4;
    }
    else if (3 == field_val)
    {
        *granu = 32;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_efd_set_global_ctl(uint8 lchip, ctc_efd_global_control_type_t type, void* value)
{
    uint32 field_val = 0;
    uint32 cmd = 0;
    uint32 granu = 0;
    uint32 interval = 0;

    SYS_EFD_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(value);

    switch(type)
    {
    case CTC_EFD_GLOBAL_MODE_TCP:
        field_val = *(bool *)value ? 1 : 0;
        cmd = DRV_IOW(IpeEfdCtl_t, IpeEfdCtl_onlyTcpPktDoEfd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_PRIORITY_EN:
        field_val = *(bool *)value ? 1 : 0;
        cmd = DRV_IOW(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowPriorityValid_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_PRIORITY:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, SYS_QOS_CLASS_PRIORITY_MAX);
        field_val = *(uint32*)value;
        cmd = DRV_IOW(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowPriority_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_COLOR_EN:
        field_val = *(bool *)value ? 1 : 0;
        cmd = DRV_IOW(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowColorValid_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_COLOR:
        CTC_VALUE_RANGE_CHECK(*(uint32*)value, 1, 3);
        field_val = *(uint32*)value;
        cmd = DRV_IOW(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowColor_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_MIN_PKT_LEN:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 0x3fff);
        field_val = *(uint32*)value ? 1 : 0;
        cmd = DRV_IOW(IpeEfdCtl_t, IpeEfdCtl_elephantPacketLenEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = *(uint32*)value;
        cmd = DRV_IOW(IpeEfdCtl_t, IpeEfdCtl_minElephantPacketLen_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_DETECT_RATE:
        CTC_ERROR_RETURN(_sys_goldengate_efd_get_granu(lchip, &granu));
        CTC_ERROR_RETURN(sys_goldengate_efd_get_global_ctl(lchip, CTC_EFD_GLOBAL_DETECT_INTERVAL, &interval));

        field_val = ((*(uint32*)value) * interval / granu /8);
        CTC_MAX_VALUE_CHECK(field_val, 0x7FFFFF);
        cmd = DRV_IOW(EfdEngineCtl_t, EfdEngineCtl_elephantByteVolumeThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_IPG_EN:
        field_val = *(bool *)value ? 1 : 0;
        cmd = DRV_IOW(IpeEfdCtl_t, IpeEfdCtl_ipgEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_DETECT_GRANU:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 3);
        field_val = *(uint32*)value;
        cmd = DRV_IOW(EfdEngineCtl_t, EfdEngineCtl_efdCountShift_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_DETECT_INTERVAL:
        field_val = (*(uint32*)value) * 10;
        CTC_MAX_VALUE_CHECK(field_val, 0x3fffffff);
        cmd = DRV_IOW(EfdEngineTimerCtl_t, EfdEngineTimerCtl_cfgRefDivCountUpdRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        cmd = DRV_IOW(EfdEngineTimerCtl_t, EfdEngineTimerCtl_cfgRefDivAgingRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_AGING_MULT:
        CTC_MAX_VALUE_CHECK(*(uint32*)value, 7);
        field_val = *(uint32*)value;
        cmd = DRV_IOW(EfdEngineTimerCtl_t, EfdEngineTimerCtl_flowLetStateEvictionThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;

    case CTC_EFD_GLOBAL_REDIRECT:
        {
            uint32 *array = (uint32*)value;/*array[0] is flowId, array[1] is nhId*/
            uint32 dsfwdPtr = 0;
            CTC_MAX_VALUE_CHECK(array[0], 0x7ff);
            CTC_ERROR_RETURN(sys_goldengate_nh_get_dsfwd_offset(lchip, array[1], &dsfwdPtr));
            cmd = DRV_IOW(DsElephantFlowState_t, DsElephantFlowState_dsFwdPtr_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, array[0], cmd, &dsfwdPtr));
        }
        break;

    default:
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_efd_get_global_ctl(uint8 lchip, ctc_efd_global_control_type_t type, void* value)
{
    uint32 field_val = 0;
    uint32 field_val1 = 0;
    uint32 cmd = 0;
    uint32 granu = 0;
    uint32 interval = 0;

    SYS_EFD_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(value);

    switch(type)
    {
    case CTC_EFD_GLOBAL_MODE_TCP:
        cmd = DRV_IOR(IpeEfdCtl_t, IpeEfdCtl_onlyTcpPktDoEfd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_EFD_GLOBAL_PRIORITY_EN:
        cmd = DRV_IOR(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowPriorityValid_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_EFD_GLOBAL_PRIORITY:
        cmd = DRV_IOR(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowPriority_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32 *)value = field_val;
        break;

    case CTC_EFD_GLOBAL_COLOR_EN:
        cmd = DRV_IOR(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowColorValid_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_EFD_GLOBAL_COLOR:
        cmd = DRV_IOR(IpeAclQosCtl_t, IpeAclQosCtl_elephantFlowColor_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32 *)value = field_val;
        break;

    case CTC_EFD_GLOBAL_MIN_PKT_LEN:
        cmd = DRV_IOR(IpeEfdCtl_t, IpeEfdCtl_elephantPacketLenEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        cmd = DRV_IOR(IpeEfdCtl_t, IpeEfdCtl_minElephantPacketLen_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val1));

        *(uint32 *)value = field_val ? field_val1 : 0;
        break;

    case CTC_EFD_GLOBAL_DETECT_RATE:
        CTC_ERROR_RETURN(_sys_goldengate_efd_get_granu(lchip, &granu));
        CTC_ERROR_RETURN(sys_goldengate_efd_get_global_ctl(lchip, CTC_EFD_GLOBAL_DETECT_INTERVAL, &interval));
        if (0 == interval)
        {
            return CTC_E_INVALID_CONFIG;
        }

        cmd = DRV_IOR(EfdEngineCtl_t, EfdEngineCtl_elephantByteVolumeThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        *(uint32 *)value = field_val * 8 * granu / interval;
        break;

    case CTC_EFD_GLOBAL_IPG_EN:
        cmd = DRV_IOR(IpeEfdCtl_t, IpeEfdCtl_ipgEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(bool *)value = field_val?TRUE:FALSE;
        break;

    case CTC_EFD_GLOBAL_DETECT_GRANU:
        cmd = DRV_IOR(EfdEngineCtl_t, EfdEngineCtl_efdCountShift_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32 *)value = field_val;
        break;

    case CTC_EFD_GLOBAL_DETECT_INTERVAL:
        cmd = DRV_IOR(EfdEngineTimerCtl_t, EfdEngineTimerCtl_cfgRefDivCountUpdRstPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32 *)value = field_val/10;
        break;

    case CTC_EFD_GLOBAL_AGING_MULT:
        cmd = DRV_IOR(EfdEngineTimerCtl_t, EfdEngineTimerCtl_flowLetStateEvictionThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        *(uint32 *)value = field_val;
        break;

    case CTC_EFD_GLOBAL_FLOW_ACTIVE_BMP:
        {
            cmd = DRV_IOR(EfdElephantFlowBitmapCtl_t, EfdElephantFlowBitmapCtl_entryValid_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, (uint32*)(value))); /* this value PTR should at least have 2K bits */
        }
        break;

    default:
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_efd_register_cb(uint8 lchip, ctc_efd_fn_t callback, void* userdata)
{
    SYS_EFD_INIT_CHECK(lchip);

    SYS_EFD_LOCK(lchip);
    p_gg_efd_master[lchip]->func = callback;
    p_gg_efd_master[lchip]->userdata = userdata;
    SYS_EFD_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_global_efd_init(uint8 lchip, uint8 enable)
{
    uint32 cmd = 0;
    uint32 interval = 0;
    uint32 value = 0;
    IpeEfdCtl_m efd_ctl;
    EfdEngineCtl_m efd_engine_ctl;
    EfdEngineTimerCtl_m efd_timer_ctl;
    IpePortEfdCtl_m port_efd_ctl;
    IpeFwdCtl_m ipe_fwd_ctl;

    sal_memset(&efd_ctl, 0, sizeof(efd_ctl));
    sal_memset(&efd_engine_ctl, 0, sizeof(efd_engine_ctl));
    sal_memset(&efd_timer_ctl, 0, sizeof(efd_timer_ctl));
    sal_memset(&port_efd_ctl, 0, sizeof(port_efd_ctl));
    sal_memset(&ipe_fwd_ctl, 0, sizeof(ipe_fwd_ctl));

    if (enable)
    {
        /* set threshold */
        SetEfdEngineCtl(V, conflictBypassEfdUpCnt_f, &efd_engine_ctl, 1);
        SetEfdEngineCtl(V, elephantByteVolumeThrd_f, &efd_engine_ctl, 1);
        SetEfdEngineCtl(V, efdCountShift_f,          &efd_engine_ctl, 0);       /* 0 means shift right 4bit, default unit = 16Byte*/
        SetEfdEngineCtl(V, elephantByteVolumeThrd_f, &efd_engine_ctl, 12000);     /* actual threshold = 12000*16Byte = 192000byte/Tsunit, Tsunit = 30ms. Total 50M bps */
        cmd = DRV_IOW(EfdEngineCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &efd_engine_ctl));

        /* set update timer */
        SetEfdEngineTimerCtl(V, efdUpdMinPtr_f, &efd_timer_ctl, 0);
        SetEfdEngineTimerCtl(V, efdUpdMaxPtr_f, &efd_timer_ctl, 0x3FF);
        SetEfdEngineTimerCtl(V, efdUpdEn_f, &efd_timer_ctl, 1);
        SetEfdEngineTimerCtl(V, efdUpdScanMaxPtr_f, &efd_timer_ctl, 0x3FF);

        SetEfdEngineTimerCtl(V, cfgRefDivCountUpdRstPulse_f,    &efd_timer_ctl, 300);       /* per 30ms update one time */
        SetEfdEngineTimerCtl(V, cfgResetDivCountUpdRstPulse_f,    &efd_timer_ctl, 0);       /* must set 0 */

        /* set flow aging */
        SetEfdEngineTimerCtl(V, flowLetStateMinPtr_f, &efd_timer_ctl, 0);
        SetEfdEngineTimerCtl(V, flowLetStateMaxPtr_f, &efd_timer_ctl, 0x7FF);
        interval = 100;
        SetEfdEngineTimerCtl(V, flowLetStateEvictionInterval_f, &efd_timer_ctl, interval);
        SetEfdEngineTimerCtl(V, flowLetStateEvictionMaxPtr_f,   &efd_timer_ctl, 0x7FF);
        SetEfdEngineTimerCtl(V, flowLetStateEvictionThrd_f,     &efd_timer_ctl, 4);         /* 4 times */
        SetEfdEngineTimerCtl(V, flowLetStateEvictionUpdEn_f,    &efd_timer_ctl, 1);

        SetEfdEngineTimerCtl(V, cfgRefDivAgingRstPulse_f,    &efd_timer_ctl, 300);          /* per 30ms scan all ptr one time */
        SetEfdEngineTimerCtl(V, cfgResetDivAgingRstPulse_f,    &efd_timer_ctl, 0);          /* must set 0 */

        cmd = DRV_IOW(EfdEngineTimerCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &efd_timer_ctl));

        /* Config pulse timer to 0.1ms, low 8 bit is decimal fraction */
        value = (625*25) << 8;
        cmd = DRV_IOW(RefDivShareEfd_t, RefDivShareEfd_cfgRefDivShareEfdPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        value = 0;
        cmd = DRV_IOW(RefDivShareEfd_t, RefDivShareEfd_cfgResetDivShareEfdPulse_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));

        /* enable exception to cpu */
        cmd = DRV_IOR(IpeFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));
        SetIpeFwdCtl(V, newElephantExceptionEn_f, &ipe_fwd_ctl, 1);
        cmd = DRV_IOW(IpeFwdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd_ctl));

        /* enable efd */
        SetIpeEfdCtl(V, efdEnable_f, &efd_ctl, 1);
        SetIpeEfdCtl(V, ipgEn_f, &efd_ctl, 0);
        cmd = DRV_IOW(IpeEfdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &efd_ctl));
    }
    else
    {
        /* disable efd */
        SetIpeEfdCtl(V, efdEnable_f, &efd_ctl, 0);
        cmd = DRV_IOW(IpeEfdCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &efd_ctl));
    }

    return CTC_E_NONE;
}

void
sys_goldengate_efd_process_isr(ctc_efd_data_t* info, void* userdata)
{
    uint8 i = 0;

    for (i=0; i<info->count; i++)
    {
        SYS_EFD_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "Efd FlowId %d is aginged !!\n", info->flow_id_array[i]);
    }
}

void
sys_goldengate_efd_sync_data(uint8 lchip, ctc_efd_data_t* info)
{
    SYS_EFD_LOCK(lchip);
    if (NULL != p_gg_efd_master[lchip] && p_gg_efd_master[lchip]->func)
    {
        p_gg_efd_master[lchip]->func(info, p_gg_efd_master[lchip]->userdata);
    }
    SYS_EFD_UNLOCK(lchip);
}

int32
sys_goldengate_efd_init(uint8 lchip)
{
    p_gg_efd_master[lchip] = (sys_efd_master_t*)mem_malloc(MEM_EFD_MODULE, sizeof(sys_efd_master_t));
    if (NULL == p_gg_efd_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_gg_efd_master[lchip], 0, sizeof(sys_efd_master_t));

    CTC_ERROR_RETURN(_sys_goldengate_global_efd_init(lchip, TRUE));

    SYS_EFD_CREATE_LOCK(lchip);
    if (!p_gg_efd_master[lchip]->func)
    {
        sys_goldengate_efd_register_cb(lchip, sys_goldengate_efd_process_isr, NULL);
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_efd_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_efd_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gg_efd_master[lchip]->mutex);
    mem_free(p_gg_efd_master[lchip]);

    return CTC_E_NONE;
}

