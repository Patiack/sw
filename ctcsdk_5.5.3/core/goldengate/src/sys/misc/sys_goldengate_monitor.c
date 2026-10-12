/**
 @file sys_goldengate_monitor.c

 @date 2009-10-19

 @version v2.0

 The file contains all monitor APIs of sys layer
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_debug.h"
#include "ctc_error.h"

#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_efd.h"

#include "sys_goldengate_monitor.h"
#include "sys_goldengate_dma.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/***************************************************************
 *
 *  Defines and Macros
 *
 ***************************************************************/
#define SYS_MONITOR_MAX_CHAN_PER_SLICE 64
#define SYS_MONITOR_MAX_CHANNEL (SYS_MONITOR_MAX_CHAN_PER_SLICE*2)
#define SYS_MONITOR_GET_CHANNEL(lport)  SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport)

#define SYS_MONITOR_BUFFER_RSV_PROF 7
#define SYS_MONITOR_LATENCY_MAX_LEVEL 7
#define SYS_MONITOR_LATENCY_MAX_THRD (0xEFFFFFFF)
#define SYS_MONITOR_BUFFER_MAX_THRD ((1<<11)-1)
#define SYS_MONITOR_SYNC_CNT 64     /* refer to SYS_DMA_MONITOR_SYNC_CNT */
#define SYS_MONITOR_DIVISION_WIDE  7
#define SYS_MONITOR_SHIFT_WIDE  5

#define SYS_MONITOR_INIT_CHECK(lchip)           \
    do {                                        \
        SYS_LCHIP_CHECK_ACTIVE(lchip);               \
        if (NULL == p_gg_monitor_master[lchip]) {  \
            return CTC_E_NOT_INIT; }            \
    } while(0)

#define SYS_MONITOR_CREATE_LOCK(lchip)                          \
    do                                                          \
    {                                                           \
        sal_mutex_create(&p_gg_monitor_master[lchip]->mutex);   \
        if (NULL == p_gg_monitor_master[lchip]->mutex)          \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_MONITOR_LOCK(lchip) \
    sal_mutex_lock(p_gg_monitor_master[lchip]->mutex)

#define SYS_MONITOR_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_monitor_master[lchip]->mutex)

#define CTC_ERROR_RETURN_MONITOR_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_monitor_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

struct sys_monitor_master_s
{
     ctc_monitor_fn_t func;
     void * user_data;
     uint32 buffer_stats[SYS_MONITOR_MAX_CHANNEL][2];
     uint64 latency_stats[SYS_MONITOR_MAX_CHANNEL][8];
     uint32 totoal_stats;
     void* p_buffer;
     sal_mutex_t *mutex;
};
typedef struct sys_monitor_master_s sys_monitor_master_t;

sys_monitor_master_t *p_gg_monitor_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

STATIC int32
_sys_goldengate_monitor_set_thrd(uint8 lchip, uint8 type, uint8 index, uint32 thrd)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 level = 0;

    if (CTC_MONITOR_BUFFER == type)
    {
        if (index == 0)
        {
            field_val = thrd;
            cmd = DRV_IOW(RaEgrPortDropProfile_t, RaEgrPortDropProfile_microBurstMinThrd_f);
        }
        else if (index == 1)
        {
            field_val = thrd;
            cmd = DRV_IOW(RaEgrPortDropProfile_t, RaEgrPortDropProfile_microBurstMaxThrd_f);
        }
        for (level = 0; level < 8; level++)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, level, cmd, &field_val));
        }
    }
    else
    {
        uint16 threshold = 0;
        uint16 shift = 0;

        sys_goldengate_common_get_compress_near_division_value(lchip, thrd,
                                SYS_MONITOR_DIVISION_WIDE, SYS_MONITOR_SHIFT_WIDE, &threshold, &shift, 0);

        field_val = threshold;
        cmd = DRV_IOW(DsLatencyMonCtl_t, DsLatencyMonCtl_array_0_latencyThrdHigh_f + index*2);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

        field_val = shift;
        cmd = DRV_IOW(DsLatencyMonCtl_t, DsLatencyMonCtl_array_0_latencyThrdShift_f + index*2);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }


    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_monitor_get_thrd(uint8 lchip, uint8 type, uint8 index, uint32* thrd)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint8 level = 0;

    if (CTC_MONITOR_BUFFER == type)
    {
        if (index == 0)
        {
            cmd = DRV_IOR(RaEgrPortDropProfile_t, RaEgrPortDropProfile_microBurstMinThrd_f);
        }
        else if (index == 1)
        {
            cmd = DRV_IOR(RaEgrPortDropProfile_t, RaEgrPortDropProfile_microBurstMaxThrd_f);
        }
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, level, cmd, &field_val));

        *thrd = field_val;
    }
    else
    {
        uint32 threshold = 0;
        uint32 shift = 0;

        cmd = DRV_IOR(DsLatencyMonCtl_t, DsLatencyMonCtl_array_0_latencyThrdHigh_f + index*2);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &threshold));

        cmd = DRV_IOR(DsLatencyMonCtl_t, DsLatencyMonCtl_array_0_latencyThrdShift_f + index*2);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &shift));
        *thrd = (threshold << shift);
    }

    return CTC_E_NONE;
}





STATIC int32
_sys_goldengate_monitor_get_timer_cfg(uint8 lchip, uint16 scan_timer, uint32* p_scan_interval,  uint32* p_max_ptr)
{
    uint32 delta = 1;
    uint32 core_freq = 0;
    uint32 min_scan_ptr = 0;

    core_freq = sys_goldengate_get_core_freq(lchip, 0);
    *p_scan_interval = (core_freq*5-1);
    *p_max_ptr = (((scan_timer*core_freq*1000) / delta) / (*p_scan_interval + 1)) + min_scan_ptr - 1;

    return CTC_E_NONE;
}



int32
sys_goldengate_monitor_set_timer(uint8 lchip, uint8 monitor_type, uint16 scan_timer)
{
    uint32 cmd = 0;

    QMgrEnqCtl_m q_mgr_enq_ctl;
    LatencyMonScanCtl_m latency_mon_scan_ctl;

    uint32 max_phy_ptr = 0;
    uint32 min_scan_ptr = 0;
    uint32 max_scan_ptr = 0;
    uint32 scan_interval = 0;

    if (monitor_type == CTC_MONITOR_BUFFER)
    {
        /*BufferMonitorTimer*/
        sal_memset(&q_mgr_enq_ctl, 0, sizeof(QMgrEnqCtl_m));
        max_phy_ptr = SYS_MONITOR_MAX_CHAN_PER_SLICE - 1;
        _sys_goldengate_monitor_get_timer_cfg(lchip, scan_timer, &scan_interval, &max_scan_ptr);
        if(max_scan_ptr > 0xFFFF)
        {
            return CTC_E_INVALID_PARAM;
        }
        cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));
        SetQMgrEnqCtl(V, maxBufScanPhyPtr_f , &q_mgr_enq_ctl,  max_phy_ptr);
        SetQMgrEnqCtl(V, minBufScanPtr_f , &q_mgr_enq_ctl, min_scan_ptr);
        SetQMgrEnqCtl(V, maxBufScanPtr_f , &q_mgr_enq_ctl, max_scan_ptr-2);/*2 is adjust for precision*/
        SetQMgrEnqCtl(V, bufScanInterval_f       , &q_mgr_enq_ctl,  scan_interval);
        cmd = DRV_IOW(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));

    }
    else
    {
        /*LatencyMonitorTimer*/
        sal_memset(&latency_mon_scan_ctl, 0, sizeof(latency_mon_scan_ctl));
        max_phy_ptr = SYS_MONITOR_MAX_CHAN_PER_SLICE - 1;
        _sys_goldengate_monitor_get_timer_cfg(lchip, scan_timer, &scan_interval, &max_scan_ptr);
        if(max_scan_ptr > 0xFFFF)
        {
            return CTC_E_INVALID_PARAM;
        }
        /* LatencyMonScanCtl index 0 */
        cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &latency_mon_scan_ctl));
        SetLatencyMonScanCtl(V, scanMaxPhyPtr_f , &latency_mon_scan_ctl, max_phy_ptr);
        SetLatencyMonScanCtl(V, scanMinPtr_f     , &latency_mon_scan_ctl, min_scan_ptr);
        SetLatencyMonScanCtl(V, scanMaxPtr_f , &latency_mon_scan_ctl, max_scan_ptr-1);/*1 is adjust for precision*/
        SetLatencyMonScanCtl(V, scanInterval_f        , &latency_mon_scan_ctl, scan_interval);
        cmd = DRV_IOW(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &latency_mon_scan_ctl));
        /* LatencyMonScanCtl index 1 */
        cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &latency_mon_scan_ctl));
        SetLatencyMonScanCtl(V, scanMaxPhyPtr_f , &latency_mon_scan_ctl, max_phy_ptr);
        SetLatencyMonScanCtl(V, scanMinPtr_f     , &latency_mon_scan_ctl, min_scan_ptr);
        SetLatencyMonScanCtl(V, scanMaxPtr_f , &latency_mon_scan_ctl, max_scan_ptr-1);/*1 is adjust for precision*/
        SetLatencyMonScanCtl(V, scanInterval_f        , &latency_mon_scan_ctl, scan_interval);
        cmd = DRV_IOW(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &latency_mon_scan_ctl));
    }
    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_monitor_get_scan_timer(uint8 lchip, uint32 *scan_timer, uint32 scan_interval, uint32 max_ptr)
{
    uint32 delta = 1;
    uint32 core_freq = 0;
    uint32 min_scan_ptr = 0;
    uint32 value = 0;
    core_freq = sys_goldengate_get_core_freq(lchip, 0);
    value = (max_ptr - min_scan_ptr + 1)*(scan_interval + 1)*delta / (core_freq*1000);
    *scan_timer = value;

    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_get_timer(uint8 lchip, uint8 monitor_type, uint32 *scan_timer)
{
    uint32 cmd = 0;
    QMgrEnqCtl_m q_mgr_enq_ctl;
    LatencyMonScanCtl_m latency_mon_scan_ctl;
    uint32 max_scan_ptr = 0;
    uint32 scan_interval = 0;

    if (monitor_type == CTC_MONITOR_BUFFER)
    {
        /*BufferMonitorTimer*/
        sal_memset(&q_mgr_enq_ctl, 0, sizeof(QMgrEnqCtl_m));
        cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));
        max_scan_ptr = GetQMgrEnqCtl(V, maxBufScanPtr_f , &q_mgr_enq_ctl);
        scan_interval = GetQMgrEnqCtl(V, bufScanInterval_f , &q_mgr_enq_ctl);
        max_scan_ptr += 2; /*adjust for precision*/
        _sys_goldengate_monitor_get_scan_timer(lchip, scan_timer, scan_interval, max_scan_ptr);


    }
    else
    {
        /*LatencyMonitorTimer*/
        sal_memset(&latency_mon_scan_ctl, 0, sizeof(latency_mon_scan_ctl));
        cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &latency_mon_scan_ctl));
        max_scan_ptr = GetLatencyMonScanCtl(V, scanMaxPtr_f , &latency_mon_scan_ctl);
        max_scan_ptr += 1; /*adjust for precision*/
        _sys_goldengate_monitor_get_scan_timer(lchip, scan_timer, scan_interval, max_scan_ptr);
        scan_interval = GetLatencyMonScanCtl(V, scanInterval_f        , &latency_mon_scan_ctl);
        _sys_goldengate_monitor_get_scan_timer(lchip, scan_timer, scan_interval, max_scan_ptr);
    }
    return CTC_E_NONE;
}

uint8
_sys_goldengate_monitor_channel_convert(uint8 lchip, uint8 chan_id)
{
    uint32 cmd = 0;
    uint8 temp = 0;
    uint32 max_scan_ptr = 0;
    uint32 scan_ptr = 0;
    QMgrEnqCtl_m q_mgr_enq_ctl;

    /*slice 0 do not need convert */
    if (chan_id < SYS_MONITOR_MAX_CHAN_PER_SLICE)
    {
        return chan_id;
    }

    /*Get scan max buf ptr */
    cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    (DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));
    max_scan_ptr = GetQMgrEnqCtl(V, maxBufScanPtr_f , &q_mgr_enq_ctl);

    scan_ptr = max_scan_ptr/2 + (chan_id - SYS_MONITOR_MAX_CHAN_PER_SLICE);
    scan_ptr = scan_ptr & 0x3F;
    temp = SYS_MONITOR_MAX_CHAN_PER_SLICE + scan_ptr;

    return temp;

}


uint8
_sys_goldengate_monitor_channel_convert_v2(uint8 lchip, uint8 chan_id)
{
    uint32 cmd = 0;
    uint32 max_scan_ptr = 0;
    uint32 scan_ptr = 0;
    uint8  temp = 0;
    QMgrEnqCtl_m q_mgr_enq_ctl;

    /*slice 1 do not need convert */
    if (chan_id >= SYS_MONITOR_MAX_CHAN_PER_SLICE)
    {
        return chan_id;
    }

    /*Get scan max buf ptr */
    cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    (DRV_IOCTL(lchip, 0, cmd, &q_mgr_enq_ctl));
    max_scan_ptr = GetQMgrEnqCtl(V, maxBufScanPtr_f , &q_mgr_enq_ctl);

    scan_ptr = chan_id - max_scan_ptr/2;
    temp = scan_ptr & 0x3F;

    return temp;

}

int32
sys_goldengate_monitor_set_config(uint8 lchip, ctc_monitor_config_t *p_cfg)
{
    uint32 cmd = 0;
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 channel_id = 0;
    uint8 channel_tmp = 0;
    uint32 field_val = 0;
    ctc_chip_device_info_t dev_info;
    QMgrEnqCtl_m qm_ctl;
    LatencyMonScanCtl_m latency_ctl0;
    LatencyMonScanCtl_m latency_ctl1;
    uint32 monitor_en[4] = {0};
    uint32 latency_en0[2] = {0};
    uint32 latency_en1[2] = {0};

    field_val = p_cfg->value?1:0;

    if((p_cfg->monitor_type == CTC_MONITOR_BUFFER && CTC_MONITOR_CONFIG_MON_SCAN_EN == p_cfg->cfg_type)
        || (p_cfg->monitor_type == CTC_MONITOR_LATENCY && CTC_MONITOR_CONFIG_MON_INTERVAL != p_cfg->cfg_type))
    {
        gport = p_cfg->gport;
        SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
        channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
        if (channel_id == 0xFF)
        {
            return CTC_E_INVALID_PARAM;
        }
    }

    switch(p_cfg->cfg_type)
    {
    case CTC_MONITOR_CONFIG_MON_INFORM_EN:
        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_microBurstInformEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
#if 0
           field_val = p_cfg->value?0:SYS_MONITOR_BUFFER_RSV_PROF;
            cmd = DRV_IOW(RaEgrPortCtl_t, RaEgrPortCtl_dropProfId_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
#endif

        }
        else
        {
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyActMonEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyInformEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyWatermarkEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }
        break;

    case CTC_MONITOR_CONFIG_MON_INFORM_MIN:
        field_val = p_cfg->value;
        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            field_val = (field_val >> 4);
            CTC_MAX_VALUE_CHECK(field_val, SYS_MONITOR_BUFFER_MAX_THRD);
            _sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_BUFFER, 0, field_val);
        }
        else
        {
            CTC_MAX_VALUE_CHECK(field_val, SYS_MONITOR_LATENCY_MAX_LEVEL);
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyInformMinThrdIdx_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }

        break;

    case CTC_MONITOR_CONFIG_MON_INFORM_MAX:
        field_val = p_cfg->value;
        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            field_val = (field_val >> 4);
            CTC_MAX_VALUE_CHECK(field_val, SYS_MONITOR_BUFFER_MAX_THRD);
            _sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_BUFFER, 1, field_val);
        }
        else
        {
            CTC_MAX_VALUE_CHECK(field_val, SYS_MONITOR_LATENCY_MAX_LEVEL);
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyInformMaxThrdIdx_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }

        break;


    case CTC_MONITOR_CONFIG_LOG_EN:

        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            cmd = DRV_IOW(EpeMiscLogCtl_t, EpeMiscLogCtl_microBurstLogEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        else
        {
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyLogEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }

        break;

    case CTC_MONITOR_CONFIG_LOG_THRD_LEVEL:
        if (p_cfg->monitor_type == CTC_MONITOR_LATENCY)
        {
            field_val = p_cfg->value;
            CTC_MAX_VALUE_CHECK(field_val, SYS_MONITOR_LATENCY_MAX_LEVEL);
            cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyLogIdx_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }

        break;

    case CTC_MONITOR_CONFIG_MON_SCAN_EN:

        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            uint32 value[4];
            ds_t ds;

            sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
            sys_goldengate_chip_get_device_info(lchip, &dev_info);
            if (dev_info.version_id == 1)
            {
                channel_tmp = _sys_goldengate_monitor_channel_convert(lchip, channel_id);
            }
            else
            {
                channel_tmp = _sys_goldengate_monitor_channel_convert_v2(lchip, channel_id);
            }

            cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
            GetQMgrEnqCtl(A,  activeBufMonChanEn_f, ds, value);
            if (p_cfg->value)
            {
                CTC_BIT_SET(value[channel_tmp >> 5], channel_tmp&0x1F);
            }
            else
            {
                CTC_BIT_UNSET(value[channel_tmp >> 5], channel_tmp&0x1F);
            }
            SetQMgrEnqCtl(A,  activeBufMonChanEn_f, ds, value);
            cmd = DRV_IOW(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
        }
        else
        {
            uint32 value[2];
            ds_t ds;
            uint8 index = 0;
            index = (channel_id < SYS_MONITOR_MAX_CHAN_PER_SLICE)?0:1;
            channel_id = channel_id&0x3F;

            cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
            GetLatencyMonScanCtl(A,  reportEn_f, ds, value);
            if (p_cfg->value)
            {
                CTC_BIT_SET(value[channel_id >> 5], channel_id&0x1F);
            }
            else
            {
                CTC_BIT_UNSET(value[channel_id >> 5], channel_id&0x1F);
            }
            SetLatencyMonScanCtl(A,  reportEn_f, ds, value);
            cmd = DRV_IOW(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
        }

        /*check channel monitor enable*/
        cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qm_ctl));
        GetQMgrEnqCtl(A,  activeBufMonChanEn_f, &qm_ctl, monitor_en);

        cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &latency_ctl0));
        GetLatencyMonScanCtl(A,  reportEn_f, &latency_ctl0, latency_en0);

        cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &latency_ctl1));
        GetLatencyMonScanCtl(A,  reportEn_f, &latency_ctl1, latency_en1);

        if (monitor_en[0] || monitor_en[1] || monitor_en[2] || monitor_en[3] ||
            latency_en0[0] || latency_en0[1] || latency_en1[0] || latency_en1[1])
        {
            /*monitor scan enable should disable dma monitor timer*/
            CTC_ERROR_RETURN(sys_goldengate_dma_set_monitor_timer(lchip, 0));
        }
        else
        {
            /*monitor scan disable should enable dma monitor timer, timer cfg 1s for event export*/
            CTC_ERROR_RETURN(sys_goldengate_dma_set_monitor_timer(lchip, 1000));
        }

        break;

    case  CTC_MONITOR_CONFIG_MON_INTERVAL:
        CTC_ERROR_RETURN(sys_goldengate_monitor_set_timer(lchip, p_cfg->monitor_type, p_cfg->value));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_monitor_get_config(uint8 lchip, ctc_monitor_config_t *p_cfg)
{
    uint32 cmd = 0;
    uint16 lport = 0;
    uint16 gport = 0;
    uint8 channel_id = 0;
    uint8 channel_tmp = 0;
    uint32 field_val = 0;
    ctc_chip_device_info_t dev_info;

    gport = p_cfg->gport;
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);

    switch(p_cfg->cfg_type)
    {
    case CTC_MONITOR_CONFIG_MON_INFORM_EN:
        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            cmd = DRV_IOR(QMgrEnqCtl_t, QMgrEnqCtl_microBurstInformEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        else
        {
            cmd = DRV_IOR(DsDestChannel_t, DsDestChannel_latencyActMonEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }
         p_cfg->value = field_val;
        break;
    case CTC_MONITOR_CONFIG_MON_INFORM_MIN:
        field_val = p_cfg->value;
        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            field_val = (field_val >> 4);
            _sys_goldengate_monitor_get_thrd(lchip, CTC_MONITOR_BUFFER, 0, &field_val);
            field_val = (field_val << 4);
        }
        else
        {
            cmd = DRV_IOR(DsDestChannel_t, DsDestChannel_latencyInformMinThrdIdx_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }
        p_cfg->value = field_val;
        break;

    case CTC_MONITOR_CONFIG_MON_INFORM_MAX:
        field_val = p_cfg->value;
        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            field_val = (field_val >> 4);
            _sys_goldengate_monitor_get_thrd(lchip, CTC_MONITOR_BUFFER, 1, &field_val);
            field_val = (field_val << 4);
        }
        else
        {
            cmd = DRV_IOR(DsDestChannel_t, DsDestChannel_latencyInformMaxThrdIdx_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }
        p_cfg->value = field_val;
        break;


    case CTC_MONITOR_CONFIG_LOG_EN:

        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            cmd = DRV_IOR(EpeMiscLogCtl_t, EpeMiscLogCtl_microBurstLogEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        }
        else
        {
            cmd = DRV_IOR(DsDestChannel_t, DsDestChannel_latencyLogEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }
        p_cfg->value = field_val;
        break;

    case CTC_MONITOR_CONFIG_LOG_THRD_LEVEL:
        if (p_cfg->monitor_type == CTC_MONITOR_LATENCY)
        {
            field_val = p_cfg->value;
            cmd = DRV_IOR(DsDestChannel_t, DsDestChannel_latencyLogIdx_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        }
        p_cfg->value = field_val;
        break;

    case CTC_MONITOR_CONFIG_MON_SCAN_EN:

        if (p_cfg->monitor_type == CTC_MONITOR_BUFFER)
        {
            uint32 fvalue[4];
            ds_t ds;

            sal_memset(&dev_info, 0, sizeof(ctc_chip_device_info_t));
            sys_goldengate_chip_get_device_info(lchip, &dev_info);
            if (dev_info.version_id == 1)
            {
                channel_tmp = _sys_goldengate_monitor_channel_convert(lchip, channel_id);
            }
            else
            {
                channel_tmp = _sys_goldengate_monitor_channel_convert_v2(lchip, channel_id);
            }

            cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
            GetQMgrEnqCtl(A,  activeBufMonChanEn_f, ds, fvalue);
            field_val = CTC_IS_BIT_SET(fvalue[channel_tmp >> 5], channel_tmp&0x1F);

        }
        else
        {
            uint32 fvalue[2];
            ds_t ds;
            uint8 index = 0;
            index = (channel_id < SYS_MONITOR_MAX_CHAN_PER_SLICE)?0:1;
            channel_id = channel_id&0x3F;

            cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, ds));
            GetLatencyMonScanCtl(A,  reportEn_f, ds, fvalue);
            field_val = CTC_IS_BIT_SET(fvalue[channel_id >> 5], channel_id&0x1F);

        }
        p_cfg->value = field_val;
        break;

    case  CTC_MONITOR_CONFIG_MON_INTERVAL:
        CTC_ERROR_RETURN(sys_goldengate_monitor_get_timer(lchip, p_cfg->monitor_type, &p_cfg->value));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;

}


int32
sys_goldengate_monitor_get_watermark(uint8 lchip, ctc_monitor_watermark_t *p_watermark)
{
   uint32 cmd = 0;
   uint16 lport = 0;
   uint8 channel_id = 0;
   uint32 uc_cnt = 0;
   uint32 mc_cnt = 0;
   uint32 total_cnt = 0;
   ds_t ds = {0};

   SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_watermark->gport, lchip, lport);
   channel_id = SYS_GET_CHANNEL_ID(lchip, p_watermark->gport);

   if (p_watermark->monitor_type == CTC_MONITOR_BUFFER)
   {
       if(CTC_EGRESS == p_watermark->u.buffer.dir)
       {
           if (channel_id < SYS_MONITOR_MAX_CHAN_PER_SLICE)
           {
               cmd = DRV_IOR(RaEgrPortMaxCnt0_t, RaEgrPortMaxCnt0_ucastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &uc_cnt));
               cmd = DRV_IOR(RaEgrPortMaxCnt0_t, RaEgrPortMaxCnt0_mcastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &mc_cnt));
               cmd = DRV_IOR(RaEgrPortMaxCnt0_t, RaEgrPortMaxCnt0_totalMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &total_cnt));
           }
           else
           {
               cmd = DRV_IOR(RaEgrPortMaxCnt1_t, RaEgrPortMaxCnt1_ucastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &uc_cnt));
               cmd = DRV_IOR(RaEgrPortMaxCnt1_t, RaEgrPortMaxCnt1_mcastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &mc_cnt));
               cmd = DRV_IOR(RaEgrPortMaxCnt1_t, RaEgrPortMaxCnt1_totalMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &total_cnt));
           }
           p_watermark->u.buffer.max_uc_cnt = uc_cnt;
           p_watermark->u.buffer.max_mc_cnt = mc_cnt;
           p_watermark->u.buffer.max_total_cnt = total_cnt;
       }
       else
       {
           return CTC_E_NOT_SUPPORT;
       }
   }
   else
   {
       uint32 value[2];
       cmd = DRV_IOR(DsLatencyWatermark_t, DRV_ENTRY_FLAG);
       CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, ds));
       GetDsLatencyWatermark(A,  latencyMax_f, ds,   value);
       SYS_UINT64_SET(p_watermark->u.latency.max_latency, value);

   }
    return CTC_E_NONE;
}

int32
sys_goldengate_monitor_clear_watermark(uint8 lchip, ctc_monitor_watermark_t *p_watermark)
{
   uint32 cmd = 0;
   uint16 lport = 0;
   uint8 channel_id = 0;
   uint32 uc_cnt = 0;
   uint32 mc_cnt = 0;
   uint32 total_cnt = 0;
   ds_t ds = {0};

   SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_watermark->gport, lchip, lport);
   channel_id = SYS_GET_CHANNEL_ID(lchip, p_watermark->gport);

   if (p_watermark->monitor_type == CTC_MONITOR_BUFFER)
   {
       if(CTC_EGRESS == p_watermark->u.buffer.dir)
       {
           if (channel_id < SYS_MONITOR_MAX_CHAN_PER_SLICE)
           {
               cmd = DRV_IOW(RaEgrPortMaxCnt0_t, RaEgrPortMaxCnt0_ucastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &uc_cnt));
               cmd = DRV_IOW(RaEgrPortMaxCnt0_t, RaEgrPortMaxCnt0_mcastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &mc_cnt));
               cmd = DRV_IOW(RaEgrPortMaxCnt0_t, RaEgrPortMaxCnt0_totalMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &total_cnt));
           }
           else
           {
               cmd = DRV_IOW(RaEgrPortMaxCnt1_t, RaEgrPortMaxCnt1_ucastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &uc_cnt));
               cmd = DRV_IOW(RaEgrPortMaxCnt1_t, RaEgrPortMaxCnt1_mcastMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &mc_cnt));
               cmd = DRV_IOW(RaEgrPortMaxCnt1_t, RaEgrPortMaxCnt1_totalMaxCnt_f);
               CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id&0x3F, cmd, &total_cnt));
           }
       }
       else
       {
           return CTC_E_NOT_SUPPORT;
       }

   }
   else
   {
       uint32 value[2]={0};
       cmd = DRV_IOW(DsLatencyWatermark_t, DRV_ENTRY_FLAG);
       CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, ds));
       SetDsLatencyWatermark(A,  latencyMax_f, ds,   value);


   }
    return CTC_E_NONE;

}


int32
sys_goldengate_monitor_sync_latency(uint8 lchip, ctc_monitor_data_t *p_data,
                                     DsLatencyMonInfo_m * p_latency, uint64* p_ts)
{
     uint8 info_type = 0;
     uint16 channel_id = 0;
     uint8 idx = 0;
     uint8 gchip = 0;
     uint16 lport = 0;
     uint16 gport = 0;
     uint8 msg_idx = 0;
     ctc_monitor_msg_t *p_msg = NULL;
     uint32 value[2] = {0};

     msg_idx = p_data->msg_num;
     p_msg =  (ctc_monitor_msg_t*)p_data->p_msg +msg_idx;
     p_msg->monitor_type = CTC_MONITOR_LATENCY;
     p_msg->timestamp = *p_ts;
     CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
     p_msg->gchip = gchip;
     channel_id = GetDsLatencyMonInfo(V, channelId_f, p_latency);
     /*Need mapping from channel to lport*/
     lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(SYS_GET_LPORT_ID_WITH_CHAN(lchip, channel_id));
     sys_goldengate_get_gchip_id(lchip, &gchip);
     gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);

     info_type = GetDsLatencyMonInfo(V, infoType_f, p_latency);
     if (0 == info_type)
     {
         p_msg->msg_type = CTC_MONITOR_MSG_STATS;
         p_msg->u.latency_stats.gport = gport;

         for (idx = 0; idx < 8; idx++)
         {
             p_msg->u.latency_stats.threshold_cnt[idx]
             =  GetDsLatencyMonInfo(V, data_latencyMonData_0_latencyThrdCnt_f + idx, p_latency);
             p_gg_monitor_master[lchip]->latency_stats[channel_id][idx] += p_msg->u.latency_stats.threshold_cnt[idx];
         }
     }
     else
     {
         p_msg->msg_type = CTC_MONITOR_MSG_EVENT;
         p_msg->u.latency_event.gport = gport;
         p_msg->u.latency_event.event_state = GetDsLatencyMonInfo(V, data_latencyEventData_valid_f, p_latency);
         p_msg->u.latency_event.source_port = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsLatencyMonInfo(V, data_latencyEventData_sourcePort_f, p_latency));
         p_msg->u.latency_event.port = GetDsLatencyMonInfo(V, data_latencyEventData_localPhyPort_f, p_latency);
         p_msg->u.latency_event.level = GetDsLatencyMonInfo(V, data_latencyEventData_region_f, p_latency);

         GetDsLatencyMonInfo(A,  data_latencyEventData_residenceTime_f, p_latency, value);
         SYS_UINT64_SET(p_msg->u.latency_event.latency, value);

         GetDsLatencyMonInfo(A,  data_latencyEventData_timestamp_f, p_latency, value);
         SYS_UINT64_SET(p_msg->timestamp, value);
     }

     msg_idx++;
     p_data->msg_num = msg_idx;

    return CTC_E_NONE;
}

int32
sys_goldengate_monitor_sync_buffer(uint8 lchip, ctc_monitor_data_t *p_data,
                                   DsBufferMonInfo_m * p_buffer, uint64* p_ts)
{
     uint8 info_type = 0;
     uint16 channel_id = 0;
     uint8 gchip = 0;
     uint16 lport = 0;
     uint16 gport = 0;
     uint8 valid_bitmap = 0;
     uint8 index = 0;
     uint8 msg_idx = 0;
     ctc_monitor_msg_t *p_msg = NULL;

     msg_idx = p_data->msg_num;
     p_msg = (ctc_monitor_msg_t*)p_data->p_msg +msg_idx;
     p_msg->monitor_type = CTC_MONITOR_BUFFER;
     CTC_ERROR_RETURN(sys_goldengate_get_gchip_id(lchip, &gchip));
     p_msg->gchip = gchip;

     info_type = GetDsBufferMonInfo(V, infoType_f, p_buffer);

     if (0 == info_type)
     {
         p_msg->msg_type = CTC_MONITOR_MSG_STATS;
         p_msg->timestamp = *p_ts;
         channel_id = GetDsBufferMonInfo(V, u_g1_channelId0_f , p_buffer);

         /*Need mapping from channel to lport*/
         lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(SYS_GET_LPORT_ID_WITH_CHAN(lchip, channel_id));
         sys_goldengate_get_gchip_id(lchip, &gchip);
         gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);

         p_msg->u.buffer_stats.gport = gport;
         p_msg->u.buffer_stats.buffer_cnt = GetDsBufferMonInfo(V, u_g1_ucastBufCnt0_f, p_buffer) + GetDsBufferMonInfo(V, u_g1_mcastBufCnt0_f, p_buffer);
         p_msg->u.buffer_stats.buffer_type = CTC_MONITOR_BUFFER_PORT;

         p_gg_monitor_master[lchip]->buffer_stats[channel_id][0] = GetDsBufferMonInfo(V, u_g1_ucastBufCnt0_f, p_buffer);
         p_gg_monitor_master[lchip]->buffer_stats[channel_id][1] = GetDsBufferMonInfo(V, u_g1_mcastBufCnt0_f, p_buffer);

         msg_idx++;
     }
     else  if (1 == info_type)
     {
         valid_bitmap = GetDsBufferMonInfo(V, u_g1_microburstInfoVec_f , p_buffer);

         for (index = 0; index < 4; index++)
         {
             if (CTC_IS_BIT_SET(valid_bitmap, (3-index)))
             {
                 p_msg->msg_type = CTC_MONITOR_MSG_EVENT;
                 p_msg->timestamp = *p_ts;
                 channel_id = GetDsBufferMonInfo(V, u_g1_channelId0_f + index, p_buffer);
                 /*Need mapping from channel to lport*/
                 lport = SYS_GET_LPORT_ID_WITH_CHAN(lport, channel_id);
                 lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);
                 sys_goldengate_get_gchip_id(lchip, &gchip);
                 gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport);

                 p_msg->u.buffer_event.gport = gport;
                 p_msg->u.buffer_event.event_state        =  GetDsBufferMonInfo(V, u_g1_microburstValid0_f + index, p_buffer);
                 p_msg->u.buffer_event.uc_cnt                 =  GetDsBufferMonInfo(V, u_g1_ucastBufCnt0_f  + index, p_buffer);
                 p_msg->u.buffer_event.mc_cnt                =  GetDsBufferMonInfo(V, u_g1_mcastBufCnt0_f + index, p_buffer);
                 p_msg->u.buffer_event.threshold_level   =  GetDsBufferMonInfo(V, u_g1_egrScCntCongestLevel0_f  + index, p_buffer);

                 msg_idx++;
                 p_msg = (ctc_monitor_msg_t*)p_data->p_msg +msg_idx;

             }
         }

     }
     else if (3 == info_type)
     {
         p_msg->msg_type = CTC_MONITOR_MSG_STATS;
         p_msg->u.buffer_stats.buffer_type = CTC_MONITOR_BUFFER_TOTAL;
         p_msg->u.buffer_stats.buffer_cnt  = GetDsBufferMonInfo(V, u_g3_totalCnt_f, p_buffer);
         p_gg_monitor_master[lchip]->totoal_stats = p_msg->u.buffer_stats.buffer_cnt;
         msg_idx++;
         p_msg = (ctc_monitor_msg_t*)p_data->p_msg +msg_idx;

         p_msg->msg_type = CTC_MONITOR_MSG_STATS;
         p_msg->u.buffer_stats.buffer_type = CTC_MONITOR_BUFFER_SC;
         p_msg->u.buffer_stats.sc = 0;
         p_msg->u.buffer_stats.buffer_cnt  =  GetDsBufferMonInfo(V, u_g3_scCnt0_f, p_buffer);
         msg_idx++;
     }

    p_data->msg_num = msg_idx;

    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_sync_data(uint8 lchip, void* p_data)
{
    uint16 entry_num = 0;
    uint16 index = 0;
    DmaActMonFifo_m* p_monitor = NULL;
    sys_dma_info_t* p_dma_info = (sys_dma_info_t*)p_data;
    ctc_monitor_data_t  monitor_data;
    ctc_efd_data_t efd_data;
    ds_t ds;
    uint32 type = 0;
    uint64 base_ts = 0;
    uint64 ts;
    uint32 value[2] = {0};

    CTC_PTR_VALID_CHECK(p_dma_info->p_ext);

    sal_memset(ds, 0, sizeof(ds));
    sal_memset(&monitor_data, 0, sizeof(monitor_data));
    sal_memset(&efd_data, 0, sizeof(efd_data));

    SYS_MONITOR_LOCK(lchip);
    monitor_data.p_msg = p_gg_monitor_master[lchip]->p_buffer;
    base_ts = *((uint64*)p_dma_info->p_ext);
    entry_num = p_dma_info->entry_num;

    for (index = 0; index < entry_num; index++)
    {
        p_monitor = ((DmaActMonFifo_m*)p_dma_info->p_data+index);

        type = GetDmaActMonFifo(V, monType_f, p_monitor);
        GetDmaActMonFifo(A, actMonData_f, p_monitor, &ds);
        GetDmaActMonFifo(A, timestamp_f, p_monitor, value);
        SYS_UINT64_SET(ts, value);
        ts += base_ts;

        if (type == 0) /* efd flow aging */
        {
            efd_data.flow_id_array[efd_data.count] = ds[0];
            efd_data.count ++;
        }

        if (type == 2 || type == 3)
        {
            DsLatencyMonInfo_m latency;
            sal_memcpy(&latency, ds, sizeof(latency));
            sys_goldengate_monitor_sync_latency(lchip, &monitor_data, &latency, &ts);
        }

        if (type == 1)
        {
            DsBufferMonInfo_m buffer;
            sal_memcpy(&buffer, ds, sizeof(buffer));
            sys_goldengate_monitor_sync_buffer(lchip, &monitor_data, &buffer, &ts);
        }

        base_ts = ts;
    }




    /*CallBack*/
    if (p_gg_monitor_master[lchip]->func)
    {
        p_gg_monitor_master[lchip]->func(&monitor_data, p_gg_monitor_master[lchip]->user_data);
    }
    SYS_MONITOR_UNLOCK(lchip);
    if (efd_data.count != 0)
    {
        /* process efd callback */
        sys_goldengate_efd_sync_data(lchip, &efd_data);
    }

    *((uint64*)p_dma_info->p_ext) = base_ts;

    return CTC_E_NONE;
}


/*every slice alloc 48 ports*/
int32
sys_goldengate_monitor_sync_queue_stats(uint8 lchip, void* p_data)
{
    /*sys_dma_reg_t* p_dma_reg = (sys_dma_reg_t*)p_data;
    uint8 slice_id = 0;
    uint8* p_addr = NULL;

    slice_id = *((uint8*)p_dma_reg->p_ext);
    p_addr = p_dma_reg->p_data;*/

     /*-ctc_monitor_queue_sample_export(slice_id, p_addr);*/
    /* TODO later, using common cb */
    return CTC_E_NONE;
}



int32
sys_goldengate_monitor_register_cb(uint8 lchip, ctc_monitor_fn_t callback, void* userdata)
{
    p_gg_monitor_master[lchip]->func = callback;
    p_gg_monitor_master[lchip]->user_data = userdata;
    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_get_cb(uint8 lchip, void **cb, void** user_data)
{
    *cb = p_gg_monitor_master[lchip]->func;
    *user_data = p_gg_monitor_master[lchip]->user_data;
    return CTC_E_NONE;
}



int32
sys_goldengate_monitor_set_global_config(uint8 lchip, ctc_monitor_glb_cfg_t* p_cfg)
{
    uint32 field_val               = 0;
    uint32 cmd                     = 0;

    switch(p_cfg->cfg_type)
    {
    case  CTC_MONITOR_GLB_CONFIG_LATENCY_THRD:
        CTC_MAX_VALUE_CHECK(p_cfg->u.thrd.level, SYS_MONITOR_LATENCY_MAX_LEVEL);
        CTC_MAX_VALUE_CHECK(p_cfg->u.thrd.threshold, SYS_MONITOR_LATENCY_MAX_THRD);
        CTC_ERROR_RETURN(
            _sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, p_cfg->u.thrd.level, p_cfg->u.thrd.threshold));

        break;

    case  CTC_MONITOR_GLB_CONFIG_BUFFER_LOG_MODE:
        CTC_MAX_VALUE_CHECK(p_cfg->u.value, 1);
        field_val = p_cfg->u.value?1:0;
        cmd = DRV_IOW(EpeMiscLogCtl_t, EpeMiscLogCtl_microBurstLogVictims_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
        break;


    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_init_global(uint8 lchip, void * p_global_cfg)
{
    uint32 cmd = 0;
    uint32 field_val = 0;

    /*RegPortMbState_t*/
    /*RaEgrPortDropProfile_microBurstMinThrd_f*/
    /*RaEgrPortDropProfile_microBurstMaxThrd_f*/

    /*
    RaEgrPortMaxCnt0_mcastMaxCnt_
    RaEgrPortMaxCnt0_totalMaxCnt_f
    RaEgrPortMaxCnt0_ucastMaxCnt_f

    RaEgrPortMaxCnt1_mcastMaxCnt_f
    RaEgrPortMaxCnt1_totalMaxCnt_f
    RaEgrPortMaxCnt1_ucastMaxCnt_f
    */


    /*EpePktProcCtl_latencyInformEn_f*/
    field_val = 1;
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_latencyInformEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(LatencyMonScanCtl_t, LatencyMonScanCtl_scanEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    cmd = DRV_IOW(LatencyMonScanCtl_t, LatencyMonScanCtl_scanEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_val));


    field_val = 1;
    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_activeBufMonEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 0xFFFF;
    cmd = DRV_IOW(EpeMiscLogCtl_t, EpeMiscLogCtl_randomThreshold_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(EpeMiscLogCtl_t, EpeMiscLogCtl_microBurstLogVictims_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 1;
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_latencyLogEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOR(EpeHdrEditReserved0_t, EpeHdrEditReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = field_val | 0x01;
    cmd = DRV_IOW(EpeHdrEditReserved0_t, EpeHdrEditReserved0_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOR(EpeHdrEditReserved1_t, EpeHdrEditReserved1_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    field_val = field_val | 0x01;
    cmd = DRV_IOW(EpeHdrEditReserved1_t, EpeHdrEditReserved1_reserved_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    field_val = 64;
    cmd = DRV_IOW(EpePktProcCtl2_t, EpePktProcCtl2_channelIdBase_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, &field_val));


#if 0
    field_val = 0x3FF;
    for (i = 0; i < 8; i++)
    {
        cmd = DRV_IOW(RaEgrPortDropProfile_t, RaEgrPortDropProfile_microBurstMinThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MONITOR_BUFFER_RSV_PROF*8 + i, cmd, &field_val));
        cmd = DRV_IOW(RaEgrPortDropProfile_t, RaEgrPortDropProfile_microBurstMaxThrd_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, SYS_MONITOR_BUFFER_RSV_PROF*8 + i, cmd, &field_val));
    }

    for (i = 0; i < 128; i++)
    {
        field_val = SYS_MONITOR_BUFFER_RSV_PROF;
        cmd = DRV_IOW(RaEgrPortCtl_t, RaEgrPortCtl_dropProfId_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, i, cmd, &field_val));

    }
#endif


    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 0, 0));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 1, 512));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 2, 768));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 3, 1024));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 4, 1280));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 5, 1536));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 6, 1792));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_LATENCY, 7, 2048));
    CTC_ERROR_RETURN(_sys_goldengate_monitor_set_thrd(lchip, CTC_MONITOR_BUFFER, 1, SYS_MONITOR_BUFFER_MAX_THRD<<4));

    return CTC_E_NONE;
}

int32
sys_goldengate_monitor_show_status(uint8 lchip)
{
    uint8 level = 0;
    uint32 cmd = 0;
    uint32 threshold = 0;
    uint32 interval = 0;
    uint32 field_val = 0;
    char* log_mode[2] = {"causer", "victim"};

    SYS_MONITOR_INIT_CHECK(lchip);

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------\n");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Buffer Status:\n");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------\n");
    sys_goldengate_monitor_get_timer(lchip, CTC_MONITOR_BUFFER, &interval);
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Scan intervel: %d<ms>\n", interval);
    cmd = DRV_IOR(EpeMiscLogCtl_t, EpeMiscLogCtl_microBurstLogVictims_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "log mode     : %s\n", log_mode[field_val]);

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n\n----------------------------------------\n");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Latency Status:\n");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "----------------------------------------\n");
    sys_goldengate_monitor_get_timer(lchip, CTC_MONITOR_LATENCY, &interval);
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Scan intervel: %d<ms>\n", interval);

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n%-10s  %-10s\n", "level", "threshold");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "-------------------------\n");
    for (level = 0; level < 8; level++)
    {
        _sys_goldengate_monitor_get_thrd(lchip, CTC_MONITOR_LATENCY, level, &threshold);
        SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-10d  %10d\n", level, threshold);
    }

    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_clear_stats(uint8 lchip,  uint16 gport)
{
    uint8 channel_id = 0;
    uint16 lport = 0;
    uint8 i = 0;

    SYS_MONITOR_INIT_CHECK(lchip);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (channel_id == 0xFF)
    {
        return CTC_E_INVALID_PARAM;
    }
    SYS_MONITOR_LOCK(lchip);
    p_gg_monitor_master[lchip]->buffer_stats[channel_id][0] = 0;
    p_gg_monitor_master[lchip]->buffer_stats[channel_id][1] = 0;

    for (i = 0; i < 8; i++)
    {
        p_gg_monitor_master[lchip]->latency_stats[channel_id][i] = 0;
    }
    SYS_MONITOR_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_show_stats(uint8 lchip, uint16 gport)
{
    uint8 i = 0;
    uint8 channel_id = 0;
    uint16 lport = 0;
    char *buffer[2] = {"unicat", "mcast"};
    char *latecny[8] = {"zone0", "zone1", "zone2", "zone3", "zone4", "zone5", "zone6", "zone7"};

    SYS_MONITOR_INIT_CHECK(lchip);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);
    channel_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (channel_id == 0xFF)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s %-10s\n", "BUFFER", "COUNT");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------\n");
    SYS_MONITOR_LOCK(lchip);
    for (i = 0; i < 2; i++)
    {
        SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s :%u\n", buffer[i],
                            p_gg_monitor_master[lchip]->buffer_stats[channel_id][i]);
    }

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n%-15s %-10s\n", "LATENCY", "COUNT");
    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "------------------------------\n");
    for (i = 0; i < 8; i++)
    {
        SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-15s :%"PRIu64"\n", latecny[i],
                            p_gg_monitor_master[lchip]->latency_stats[channel_id][i]);
    }
    SYS_MONITOR_UNLOCK(lchip);

    return CTC_E_NONE;
}


int32
sys_goldengate_monitor_testing_en(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 value[4];
    ds_t ds;
    uint32 channel_id = 0;
	uint8 i = 0;

	field_val = enable?1:0;

	for(i=0;i<4;i++)
	{
		value[i] = enable?0xFFFFFFFF:0;
	}

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_activeBufMonEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_microBurstInformEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_activeBufMonTcEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOW(QMgrEnqCtl_t, QMgrEnqCtl_activeBufMonTotalEn_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    cmd = DRV_IOR(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetQMgrEnqCtl(A,  activeBufMonChanEn_f, ds, value);
    cmd = DRV_IOW(QMgrEnqCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));


    cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));
    SetLatencyMonScanCtl(A,  reportEn_f, ds, value);
    cmd = DRV_IOW(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, ds));

    cmd = DRV_IOR(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, ds));
    SetLatencyMonScanCtl(A,  reportEn_f, ds, value);
    cmd = DRV_IOW(LatencyMonScanCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 1, cmd, ds));

    for (channel_id = 0; channel_id < 128; channel_id++)
    {
        cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyActMonEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyInformEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
        cmd = DRV_IOW(DsDestChannel_t, DsDestChannel_latencyWatermarkEn_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, channel_id, cmd, &field_val));
    }

    return 0;

}

void
sys_goldengate_monitor_process_isr(ctc_monitor_data_t * info, void* userdata)
{
    uint8 index = 0;
    uint8 i = 0;
     char* monitor_str[2] = {"B","L"};
     char* msg_str[2] = {"E","S"};
     ctc_monitor_msg_t *p_msg = NULL;

    if (info == NULL)
    {
        return;
    }

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT,"-------------------------<B,L>: Buffer, Latency <E,S>:Event,Stats-----------------------------------------\n");

    for (index = 0; index < info->msg_num; index++)
    {
        p_msg = &info->p_msg[index];

        if (p_msg->monitor_type == CTC_MONITOR_LATENCY
            && p_msg->msg_type == CTC_MONITOR_MSG_EVENT)
        {
            SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "No.%-4d %-4s%-10"PRIu64"  %s%s  %s%d %8s%04x %8s%04x %8s%-4d %8s%"PRIu64" %8s%-6d %8s%04x\n",
                      index,
                      "TS:",
                      p_msg->timestamp,
                      monitor_str[p_msg->monitor_type],
                      msg_str[p_msg->msg_type],
                      "gchip:",
                      p_msg->gchip,
                      "Gport:0x",
                      p_msg->u.latency_event.gport,
                      "SrcPort:0x",
                      p_msg->u.latency_event.source_port,
                      "State:",
                      p_msg->u.latency_event.event_state,
                      "Latency:",
                      p_msg->u.latency_event.latency,
                      "Level:",
                     p_msg->u.latency_event.level,
                      "port:0x",
                      p_msg->u.latency_event.port);
        }

        if (p_msg->monitor_type == CTC_MONITOR_LATENCY
            && p_msg->msg_type == CTC_MONITOR_MSG_STATS)
        {
            SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "No.%-4d %-4s%-10"PRIu64"  %s%s  %s%d %8s%04x %8s",
                      index,
                      "TS:",
                      p_msg->timestamp,
                      monitor_str[p_msg->monitor_type],
                      msg_str[p_msg->msg_type],
                      "gchip:",
                      p_msg->gchip,
                      "Gport:0x",
                      p_msg->u.latency_stats.gport,
                      "thrd:");

            for (i = 0; i < 8; i++)
            {
                SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, " %08d",  p_msg->u.latency_stats.threshold_cnt[i]);
            }
            SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, " \n");
        }


        if (p_msg->monitor_type == CTC_MONITOR_BUFFER
            && p_msg->msg_type == CTC_MONITOR_MSG_EVENT)
        {
            SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "No.%-4d %-4s%-10"PRIu64"  %s%s  %s%d %8s%04x %8s%-8d %8s%-8d %8s%-8d %8s%-8d\n",
                      index,
                      "TS:",
                      p_msg->timestamp,
                      monitor_str[p_msg->monitor_type],
                      msg_str[p_msg->msg_type],
                      "gchip:",
                      p_msg->gchip,
                      "Gport:0x",
                       p_msg->u.buffer_event.gport,
                       "UcCnt:",
                       p_msg->u.buffer_event.uc_cnt,
                       "McCnt:",
                       p_msg->u.buffer_event.mc_cnt,
                       "State:",
                       p_msg->u.buffer_event.event_state,
                       "CngLevel:",
                       p_msg->u.buffer_event.threshold_level);
        }

        if (p_msg->monitor_type == CTC_MONITOR_BUFFER
             && p_msg->msg_type == CTC_MONITOR_MSG_STATS)
        {
            if (p_msg->u.buffer_stats.buffer_type == CTC_MONITOR_BUFFER_PORT)
            {
                SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "No.%-4d %-4s%-10"PRIu64"  %s%s  %s%d %8s%04x %8s%-8d\n",
                          index,
                          "TS:",
                          p_msg->timestamp,
                          monitor_str[p_msg->monitor_type],
                          msg_str[p_msg->msg_type],
                          "gchip:",
                          p_msg->gchip,
                          "Gport:0x",
                          p_msg->u.buffer_stats.gport,
                          "TotalCnt:",
                          p_msg->u.buffer_stats.buffer_cnt);
            }
            else if(p_msg->u.buffer_stats.buffer_type == CTC_MONITOR_BUFFER_SC)
            {
                SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "No.%-4d %-4s%-10"PRIu64"  %s%s  %s%d %8s%-8d\n",
                          index,
                          "TS:",
                          p_msg->timestamp,
                          monitor_str[p_msg->monitor_type],
                          msg_str[p_msg->msg_type],
                          "gchip:",
                          p_msg->gchip,
                          "ScCnt:",
                          p_msg->u.buffer_stats.buffer_cnt);
            }
            else if(p_msg->u.buffer_stats.buffer_type == CTC_MONITOR_BUFFER_TOTAL)
            {
                SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "No.%-4d %-4s%-10"PRIu64"  %s%s  %s%d %8s%-8x\n",
                          index,
                          "TS:",
                          p_msg->timestamp,
                          monitor_str[p_msg->monitor_type],
                          msg_str[p_msg->msg_type],
                          "gchip:",
                          p_msg->gchip,
                          "Total:0x",
                          p_msg->u.buffer_stats.buffer_cnt);
            }
        }
    }

    SYS_MONITOR_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "----------------------------------------------------------------------------------------------------------\n");

    return;
}

int32
sys_goldengate_monitor_init(uint8 lchip, void * p_global_cfg)
{
    LCHIP_CHECK(lchip);
    if (p_gg_monitor_master[lchip])
    {
        return CTC_E_NONE;
    }
    p_gg_monitor_master[lchip] =  (sys_monitor_master_t*)mem_malloc(MEM_MONITOR_MODULE, sizeof(sys_monitor_master_t));
    if (NULL == p_gg_monitor_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_monitor_master[lchip], 0, sizeof(sys_monitor_master_t));

    p_gg_monitor_master[lchip]->p_buffer = mem_malloc(MEM_MONITOR_MODULE, sizeof(ctc_monitor_msg_t)*(SYS_MONITOR_SYNC_CNT*4));
    if (NULL == p_gg_monitor_master[lchip]->p_buffer)
    {
          return CTC_E_NO_MEMORY;
    }

    CTC_ERROR_RETURN( sys_goldengate_monitor_set_timer(lchip, CTC_MONITOR_BUFFER, 1));
    CTC_ERROR_RETURN( sys_goldengate_monitor_set_timer(lchip, CTC_MONITOR_LATENCY, 1));

    CTC_ERROR_RETURN(sys_goldengate_monitor_init_global(lchip, p_global_cfg));

    SYS_MONITOR_CREATE_LOCK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_dma_register_cb(lchip, SYS_DMA_CB_TYPE_MONITOR,
        sys_goldengate_monitor_sync_data));

    CTC_ERROR_RETURN(sys_goldengate_dma_register_cb(lchip, SYS_DMA_CB_TYPE_QUEUE_STATS,
        sys_goldengate_monitor_sync_queue_stats));

    if (!p_gg_monitor_master[lchip]->func)
    {
        sys_goldengate_monitor_register_cb(lchip, sys_goldengate_monitor_process_isr, NULL);
    }
    return CTC_E_NONE;
}

int32
sys_goldengate_monitor_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_monitor_master[lchip])
    {
        return CTC_E_NONE;
    }

    mem_free(p_gg_monitor_master[lchip]->p_buffer);

    sal_mutex_destroy(p_gg_monitor_master[lchip]->mutex);
    mem_free(p_gg_monitor_master[lchip]);

    return CTC_E_NONE;
}

