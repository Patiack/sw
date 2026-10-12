/**
 @file sys_goldengate_ipfix.c

 @author  Copyright (C) 2013 Centec Networks Inc.  All rights reserved.

 @date 2013-10-17

 @version v3.0
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/

#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_const.h"
#include "ctc_ipfix.h"

#include "sys_goldengate_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_dma.h"
#include "sys_goldengate_ipfix.h"
#include "sys_goldengate_ftm.h"
#include "sys_goldengate_l2_fdb.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
#include "sys_goldengate_port.h"

#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

struct sys_ipfix_master_s
{
    ctc_ipfix_fn_t ipfix_cb;
    uint32         max_ptr;
    uint32         aging_interval;
    uint32         exp_cnt_stats[8];     /* statistics based export reason */
    uint16         igr_sip_refer_cnt[4];     /* ingress sample interval profile involve port count */
    uint16         egr_sip_refer_cnt[4];     /* egress sample interval profile involve port count */

    uint64         exp_pkt_cnt_stats[8]; /* 0-Expired, 1-Tcp Close, 2-PktCnt Overflow, 3-ByteCnt Overflow */
    uint64         exp_pkt_byte_stats[8];/* 4-Ts Overflow, 5-Conflict, 6-New Flow, 7-Dest Change */
    void           * user_data;
    sal_mutex_t    * mutex;
};
typedef struct sys_ipfix_master_s sys_ipfix_master_t;

#define SYS_IPFIX_SAMPPLING_PKT_INTERVAL_NUM   4
#define SYS_IPFIX_MIN_AGING_INTERVAL       1   /*1s*/

sys_ipfix_master_t  *p_gg_ipfix_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

#define SYS_IPFIX_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip);\
        if (NULL == p_gg_ipfix_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_IPFIX_CREATE_LOCK(lchip)                            \
    do                                                          \
    {                                                           \
        sal_mutex_create(&p_gg_ipfix_master[lchip]->mutex);     \
        if (NULL == p_gg_ipfix_master[lchip]->mutex)            \
        {                                                       \
            CTC_ERROR_RETURN(CTC_E_FAIL_CREATE_MUTEX);          \
        }                                                       \
    } while (0)

#define SYS_IPFIX_LOCK(lchip) \
    sal_mutex_lock(p_gg_ipfix_master[lchip]->mutex)

#define SYS_IPFIX_UNLOCK(lchip) \
    sal_mutex_unlock(p_gg_ipfix_master[lchip]->mutex)

#define CTC_ERROR_RETURN_IPFIX_UNLOCK(op) \
    do \
    { \
        int32 rv = (op); \
        if (rv < 0) \
        { \
            sal_mutex_unlock(p_gg_ipfix_master[lchip]->mutex); \
            return rv; \
        } \
    } while (0)

extern void
_sys_goldengate_ipfix_process_isr(ctc_ipfix_data_t* info, void* userdata);
extern int32
sys_goldengate_ipfix_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info);
int32
sys_goldengate_ipfix_wb_sync(uint8 lchip);
int32
sys_goldengate_ipfix_wb_restore(uint8 lchip);
/****************************************************************************
 *
* Function
*
*****************************************************************************/
extern int32
sys_goldengate_ipfix_init(uint8 lchip, void* p_global_cfg)
{
    IpfixAgingTimerCtl_m aging_timer;
    IpfixEngineCtl_m    ipfix_ctl;
    uint32 cmd = 0;
    uint32 core_frequecy = 0;
    uint32 tick_interval = 0;
    uint32 max_phy_ptr = 0;
    uint32 byte_cnt[2] = {0};
    uint64 tmp = 0;
    IpeFwdCtl_m    ipe_fwd;
    ctc_ipfix_global_cfg_t ipfix_cfg;
    IpfixHashLookupCtl_m ipfix_hash;
    uint32 ipfix_max_entry_num;
    uintptr chip_id = lchip;
    uint32  field_value;

    CTC_PTR_VALID_CHECK(p_global_cfg);
    if (0 == SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPFIX))
    {
        return CTC_E_NONE;
    }

    sal_memset(&aging_timer, 0, sizeof(IpfixAgingTimerCtl_m));
    sal_memset(&ipfix_ctl, 0, sizeof(IpfixEngineCtl_m));
    sal_memset(&ipfix_cfg, 0, sizeof(ctc_ipfix_global_cfg_t));
    sal_memset(&ipfix_hash, 0, sizeof(IpfixHashLookupCtl_m));

    /*p_global_cfg is NULL, using default config, else using user define */
    sal_memcpy(&ipfix_cfg, p_global_cfg, sizeof(ctc_ipfix_global_cfg_t));

    if (p_gg_ipfix_master[lchip])
    {
        return CTC_E_NONE;
    }

    p_gg_ipfix_master[lchip] = (sys_ipfix_master_t*)mem_malloc(MEM_IPFIX_MODULE, sizeof(sys_ipfix_master_t));

    if (NULL == p_gg_ipfix_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_ipfix_master[lchip], 0, sizeof(sys_ipfix_master_t));

    SYS_IPFIX_CREATE_LOCK(lchip);
    ipfix_cfg.pkt_cnt = (ipfix_cfg.pkt_cnt == 0)?0:(ipfix_cfg.pkt_cnt - 1);

    core_frequecy = sys_goldengate_get_core_freq(lchip, 0);
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num));
    max_phy_ptr = ipfix_max_entry_num-4;

    /*default aging interval : 10s*/
    tmp = (((uint64)core_frequecy * 1000 * 1000 )/DOWN_FRE_RATE);
    tmp *= ipfix_cfg.aging_interval;
    tick_interval =  tmp / (max_phy_ptr/4);
    p_gg_ipfix_master[lchip]->max_ptr =  (tmp / tick_interval)*4;

    SetIpfixAgingTimerCtl(V, agingUpdEn_f, &aging_timer, 0);
    SetIpfixAgingTimerCtl(V, cpuAgingEn_f, &aging_timer, 0);
    SetIpfixAgingTimerCtl(V, interval_f, &aging_timer, tick_interval);
    SetIpfixAgingTimerCtl(V, maxPtr_f , &aging_timer, p_gg_ipfix_master[lchip]->max_ptr);
    SetIpfixAgingTimerCtl(V, minPtr_f , &aging_timer, 0);
    SetIpfixAgingTimerCtl(V, phyMaxPtr_f , &aging_timer, max_phy_ptr);
    cmd = DRV_IOW( IpfixAgingTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aging_timer));

   SetIpfixEngineCtl(V, bypassUpdateOp_f, &ipfix_ctl, 0);

   byte_cnt[0] = (ipfix_cfg.bytes_cnt) & 0xffffffff;
   byte_cnt[1] = (ipfix_cfg.bytes_cnt >> 32) & 0x1f;
   SetIpfixEngineCtl(A, byteCntWraparoundThre_f, &ipfix_ctl, byte_cnt);

   SetIpfixEngineCtl(V, conflictPktExportEn_f, &ipfix_ctl, ipfix_cfg.conflict_export);
   SetIpfixEngineCtl(V, countBasedSamplingMode_f , &ipfix_ctl, ipfix_cfg.sample_mode);
   SetIpfixEngineCtl(V, enableRecordMissPktCnt_f , &ipfix_ctl, 1);
   SetIpfixEngineCtl(V, isCpuSetFlowKeyMode_f , &ipfix_ctl, 0);
   SetIpfixEngineCtl(V, localPhyPortBase_f , &ipfix_ctl, 0x100);
   SetIpfixEngineCtl(V, ignorTcpClose_f , &ipfix_ctl, 0);   /*spec7.1bug*/
   SetIpfixEngineCtl(V, pktCntWraparoundThre_f , &ipfix_ctl, ipfix_cfg.pkt_cnt);
   SetIpfixEngineCtl(V, tsWraparoundThre_f , &ipfix_ctl, ipfix_cfg.times_interval);  /*1s*/
   SetIpfixEngineCtl(V, unknownPktDestIsVlanId_f, &ipfix_ctl, 0);
    /*SetIpfixEngineCtl(V, bcastPktDestIsVlanId_f, &ipfix_ctl, 1);*/

   cmd = DRV_IOW( IpfixEngineCtl_t, DRV_ENTRY_FLAG);
   CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_ctl));


    cmd = DRV_IOR( IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd));
    SetIpeFwdCtl(V, ipfixDestUseApsGroupId_f , &ipe_fwd, 1);
    SetIpeFwdCtl(V, ipfixDestUseEcmpGroupId_f , &ipe_fwd,0);
    SetIpeFwdCtl(V, ipfixIpgEn_f , &ipe_fwd, 0);
    SetIpeFwdCtl(V, ipfixTimeGranularityMode_f , &ipe_fwd, 1);  /*0-8ms; 1-1ms mode;2-16ms;3-1s*/
    cmd = DRV_IOW( IpeFwdCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_fwd));

    cmd = DRV_IOR( IpfixHashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_hash));
    SetIpfixHashLookupCtl(V, ipfixHashEn_f, &ipfix_hash, 1);
    cmd = DRV_IOW( IpfixHashLookupCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_hash));

    field_value = 1;
    cmd = DRV_IOW(EpePktProcCtl_t, EpePktProcCtl_ipfixTimeGranularityMode_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    CTC_ERROR_RETURN(sys_goldengate_dma_register_cb(lchip, SYS_DMA_CB_TYPE_IPFIX,
        sys_goldengate_ipfix_sync_data));

    if (!p_gg_ipfix_master[lchip]->ipfix_cb)
    {
        sys_goldengate_ipfix_register_cb(lchip, _sys_goldengate_ipfix_process_isr, (void*)chip_id);
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_register_callback(lchip, CTC_FTM_SPEC_IPFIX,
                sys_goldengate_ipfix_ftm_cb));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_IPFIX, sys_goldengate_ipfix_wb_sync));

    if (CTC_WB_ENABLE && CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING)
    {
        CTC_ERROR_RETURN(sys_goldengate_ipfix_wb_restore(lchip));
    }

    return CTC_E_NONE;


}

int32
sys_goldengate_ipfix_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_ipfix_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gg_ipfix_master[lchip]->mutex);
    mem_free(p_gg_ipfix_master[lchip]);

    return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_set_port_cfg(uint8 lchip, uint16 gport,ctc_ipfix_port_cfg_t* p_ipfix_port_cfg)
{

    uint32 cmd = 0;
    uint16 lport = 0;
    uint8 loop = 0;
    uint8  free_index = 0xFF;
    uint32 sample_interval[SYS_IPFIX_SAMPPLING_PKT_INTERVAL_NUM] = {0};
    uint32  sample_enable = 0;
    uint32  profile_index = 0;
    uint8 flow_type = 0;
    uint32 value = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_ipfix_port_cfg);
    CTC_MAX_VALUE_CHECK(p_ipfix_port_cfg->field_sel_id, 15);

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    if (p_ipfix_port_cfg->sample_interval > 0x1fff)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_ipfix_port_cfg->dir == CTC_INGRESS)
    {
        DsIpfixIngPortCfg_m  ipfix_cfg;
        IpfixIngSamplingProfile_m  ipfix_sampleing_cfg;

        sal_memset(&ipfix_cfg, 0, sizeof(DsIpfixIngPortCfg_m));
        sal_memset(&ipfix_sampleing_cfg, 0, sizeof(IpfixIngSamplingProfile_m));

        value = p_ipfix_port_cfg->lkup_type;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_IPFIX_LKUP_TYPE, CTC_INGRESS, value));

        if(CTC_IPFIX_HASH_LKUP_TYPE_DISABLE != p_ipfix_port_cfg->lkup_type)
        {
            value = p_ipfix_port_cfg->field_sel_id;
            CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_IPFIX_SELECT_ID, CTC_INGRESS, value));
        }

        /* set profile */
        cmd = DRV_IOR( DsIpfixIngPortCfg_t, DsIpfixIngPortCfg_samplingProfileIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &profile_index));
        if(0 != p_ipfix_port_cfg->sample_interval)
        {
            cmd = DRV_IOR( IpfixIngSamplingProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_sampleing_cfg));
            GetIpfixIngSamplingProfile(A, array_0_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[0]);
            GetIpfixIngSamplingProfile(A, array_1_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[1]);
            GetIpfixIngSamplingProfile(A, array_2_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[2]);
            GetIpfixIngSamplingProfile(A, array_3_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[3]);

            for (loop = 0 ; loop < SYS_IPFIX_SAMPPLING_PKT_INTERVAL_NUM ; loop++)
            {
                if ((sample_interval[loop] == 0) && (free_index == 0xFF))
                {
                    free_index = loop;
                }
                if (sample_interval[loop] == p_ipfix_port_cfg->sample_interval)
                {
                    free_index = loop;
                    break;
                }
            }
            if (free_index == 0xFF )
            {
                return CTC_E_EXCEED_MAX_SIZE;
            }
            if (free_index == 0)
            {
                SetIpfixIngSamplingProfile(V, array_0_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            else if(free_index == 1)
            {
                SetIpfixIngSamplingProfile(V, array_1_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            else if(free_index == 2)
            {
                SetIpfixIngSamplingProfile(V, array_2_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            else
            {
                SetIpfixIngSamplingProfile(V, array_3_ingSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }

            /* cfg profile, and update new index ref cnt */
            cmd = DRV_IOW( IpfixIngSamplingProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_sampleing_cfg));
            SYS_IPFIX_LOCK(lchip);
            p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[free_index]++;

            /* maintenance sw, update old index ref cnt if set the same interval times or change interval*/
            cmd = DRV_IOR( DsIpfixIngPortCfg_t, DsIpfixIngPortCfg_samplingEn_f);
            CTC_ERROR_RETURN_IPFIX_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &sample_enable));
            if(sample_enable && (p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[profile_index]>0))
            {
                /*update old index ref cnt*/
                p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[profile_index]--;
            }
            SYS_IPFIX_UNLOCK(lchip);
            /* cfg sample_en field */
            sample_enable = 1;
            cmd = DRV_IOW( DsIpfixIngPortCfg_t, DsIpfixIngPortCfg_samplingEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &sample_enable));
        }
        else
        {
            cmd = DRV_IOR( IpfixIngSamplingProfile_t, (IpfixIngSamplingProfile_array_0_ingSamplingPktInterval_f+profile_index));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sample_interval[profile_index]));

            SYS_IPFIX_LOCK(lchip);
            if((sample_interval[profile_index]>0) && (p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[profile_index]>0))
            {
                p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[profile_index]--;

                sample_enable = 0;
                cmd = DRV_IOW( DsIpfixIngPortCfg_t, DsIpfixIngPortCfg_samplingEn_f);
                CTC_ERROR_RETURN_IPFIX_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &sample_enable));
            }
            SYS_IPFIX_UNLOCK(lchip);
        }

        SYS_IPFIX_LOCK(lchip);
        /* clear sample interval by sw sip_refer_cnt */
        if(0 == p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[profile_index])
        {
            sample_interval[profile_index] = 0;
            cmd = DRV_IOW( IpfixIngSamplingProfile_t, (IpfixIngSamplingProfile_array_0_ingSamplingPktInterval_f+profile_index));
            CTC_ERROR_RETURN_IPFIX_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &sample_interval[profile_index]));
        }
        SYS_IPFIX_UNLOCK(lchip);

        cmd = DRV_IOR( DsIpfixIngPortCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ipfix_cfg));
        if(CTC_IPFIX_FLOW_TYPE_ALL_PACKET == p_ipfix_port_cfg->flow_type)
        {
            flow_type = 0;
        }
        else if(CTC_IPFIX_FLOW_TYPE_NON_DISCARD_PACKET == p_ipfix_port_cfg->flow_type)
        {
            flow_type = 1;
        }
        else if(CTC_IPFIX_FLOW_TYPE_DISCARD_PACKET == p_ipfix_port_cfg->flow_type)
        {
            flow_type = 2;
        }
        SetDsIpfixIngPortCfg(V, flowType_f, &ipfix_cfg, flow_type);
        SetDsIpfixIngPortCfg(V, portSessionNumLimitEn_f, &ipfix_cfg, 0);
        SetDsIpfixIngPortCfg(V, samplingProfileIndex_f, &ipfix_cfg, free_index);
        SetDsIpfixIngPortCfg(V, tcpSessionEndDetectDisable_f, &ipfix_cfg, p_ipfix_port_cfg->tcp_end_detect_disable);
        SetDsIpfixIngPortCfg(V, portSessionNumLimitEn_f, &ipfix_cfg, p_ipfix_port_cfg->learn_disable);
        cmd = DRV_IOW( DsIpfixIngPortCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ipfix_cfg));

    }
    else
    {
        DsIpfixEgrPortCfg_m  ipfix_cfg;
        IpfixEgrSamplingProfile_m  ipfix_sampleing_cfg;

        sal_memset(&ipfix_cfg, 0, sizeof(DsIpfixEgrPortCfg_m));
        sal_memset(&ipfix_sampleing_cfg, 0, sizeof(IpfixEgrSamplingProfile_m));

        value = p_ipfix_port_cfg->lkup_type;
        CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_IPFIX_LKUP_TYPE, CTC_EGRESS, value));

        if(CTC_IPFIX_HASH_LKUP_TYPE_DISABLE != p_ipfix_port_cfg->lkup_type)
        {
            value = p_ipfix_port_cfg->field_sel_id;
            CTC_ERROR_RETURN(sys_goldengate_port_set_internal_direction_property(lchip, gport, SYS_PORT_DIR_PROP_IPFIX_SELECT_ID, CTC_EGRESS, value));
        }

        /* set profile */
        cmd = DRV_IOR( DsIpfixEgrPortCfg_t, DsIpfixEgrPortCfg_samplingProfileIndex_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &profile_index));
        if(0 != p_ipfix_port_cfg->sample_interval)
        {
            cmd = DRV_IOR( IpfixEgrSamplingProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_sampleing_cfg));

            GetIpfixEgrSamplingProfile(A, array_0_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[0]);
            GetIpfixEgrSamplingProfile(A, array_1_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[1]);
            GetIpfixEgrSamplingProfile(A, array_2_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[2]);
            GetIpfixEgrSamplingProfile(A, array_3_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, &sample_interval[3]);
            for (loop = 0; loop < SYS_IPFIX_SAMPPLING_PKT_INTERVAL_NUM ; loop++)
            {
                if ((sample_interval[loop] == 0) && (free_index == 0xFF))
                {
                    free_index = loop;
                }
                if (sample_interval[loop] == p_ipfix_port_cfg->sample_interval)
                {
                    free_index = loop;
                    break;
                }
            }
            if (free_index == 0xFF)
            {
                return CTC_E_EXCEED_MAX_SIZE;
            }
            if (free_index == 0)
            {
                SetIpfixEgrSamplingProfile(V, array_0_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            else if(free_index == 1)
            {
                SetIpfixEgrSamplingProfile(V, array_1_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            else if(free_index == 2)
            {
                SetIpfixEgrSamplingProfile(V, array_2_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            else
            {
                SetIpfixEgrSamplingProfile(V, array_3_egrSamplingPktInterval_f, &ipfix_sampleing_cfg, p_ipfix_port_cfg->sample_interval);
            }
            /* cfg profile, and update new index ref cnt */
            cmd = DRV_IOW( IpfixEgrSamplingProfile_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_sampleing_cfg));
            SYS_IPFIX_LOCK(lchip);
            p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[free_index]++;

            /* maintenance sw, if set the same interval times or change interval*/
            cmd = DRV_IOR( DsIpfixEgrPortCfg_t, DsIpfixEgrPortCfg_samplingEn_f);
            CTC_ERROR_RETURN_IPFIX_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &sample_enable));
            if(sample_enable && (p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[profile_index]>0))
            {
                /* update old index ref cnt */
                p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[profile_index]--;
            }
            SYS_IPFIX_UNLOCK(lchip);
            /* and cfg sample_en field */
            sample_enable = 1;
            cmd = DRV_IOW( DsIpfixEgrPortCfg_t, DsIpfixEgrPortCfg_samplingEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &sample_enable));
        }
        else
        {
            cmd = DRV_IOR( IpfixEgrSamplingProfile_t, (IpfixEgrSamplingProfile_array_0_egrSamplingPktInterval_f+profile_index));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sample_interval[profile_index]));

            SYS_IPFIX_LOCK(lchip);
            if((sample_interval[profile_index]>0) && (p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[profile_index]>0))
            {
                p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[profile_index]--;

                sample_enable = 0;
                cmd = DRV_IOW( DsIpfixEgrPortCfg_t, DsIpfixEgrPortCfg_samplingEn_f);
                CTC_ERROR_RETURN_IPFIX_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &sample_enable));
            }
            SYS_IPFIX_UNLOCK(lchip);
        }

        SYS_IPFIX_LOCK(lchip);
        /* clear sample interval by sw sip_refer_cnt */
        if(0 == p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[profile_index])
        {
            sample_interval[profile_index] = 0;
            cmd = DRV_IOW( IpfixEgrSamplingProfile_t, (IpfixEgrSamplingProfile_array_0_egrSamplingPktInterval_f+profile_index));
            CTC_ERROR_RETURN_IPFIX_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &sample_interval[profile_index]));
        }
        SYS_IPFIX_UNLOCK(lchip);

        cmd = DRV_IOR( DsIpfixEgrPortCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ipfix_cfg))
        SetDsIpfixEgrPortCfg(V, flowType_f, &ipfix_cfg,  p_ipfix_port_cfg->flow_type);
        SetDsIpfixEgrPortCfg(V, portSessionNumLimitEn_f, &ipfix_cfg, 0);
        SetDsIpfixEgrPortCfg(V, samplingProfileIndex_f, &ipfix_cfg, free_index);
        SetDsIpfixEgrPortCfg(V, tcpSessionEndDetectDisable_f, &ipfix_cfg, p_ipfix_port_cfg->tcp_end_detect_disable);
        SetDsIpfixEgrPortCfg(V, portSessionNumLimitEn_f, &ipfix_cfg, p_ipfix_port_cfg->learn_disable);
        cmd = DRV_IOW( DsIpfixEgrPortCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ipfix_cfg));

    }
  return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_get_port_cfg(uint8 lchip, uint16 gport,ctc_ipfix_port_cfg_t* p_ipfix_port_cfg)
{
    uint32 cmd = 0;
    uint16 lport = 0;
    DsDestPort_m ds_dest_port;
    DsIpfixEgrPortCfg_m  ipfix_egr_cfg;
    IpfixEgrSamplingProfile_m  ipfix_egr_sampleing_cfg;
    DsSrcPort_m ds_src_port;
    DsIpfixIngPortCfg_m  ipfix_ing_cfg;
    IpfixIngSamplingProfile_m  ipfix_ing_sampleing_cfg;
    uint32 samping_idx = 0;
    DsPhyPortExt_m ds_phy_ext;
    uint8 flow_type = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    if (p_ipfix_port_cfg->dir  > CTC_EGRESS)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport);

    SYS_IPFIX_LOCK(lchip);

    if (p_ipfix_port_cfg->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_src_port), p_gg_ipfix_master[lchip]->mutex);

        cmd = DRV_IOR( IpfixIngSamplingProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &ipfix_ing_sampleing_cfg), p_gg_ipfix_master[lchip]->mutex);

        cmd = DRV_IOR( DsIpfixIngPortCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ipfix_ing_cfg), p_gg_ipfix_master[lchip]->mutex);

        cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_phy_ext), p_gg_ipfix_master[lchip]->mutex);

         /*p_ipfix_port_cfg->extern_en = GetDsSrcPort(V, keyMergeInnerAndOuterHdr_f, &ds_src_port);*/
        p_ipfix_port_cfg->lkup_type = GetDsSrcPort(V, ipfixHashType_f, &ds_src_port);
        p_ipfix_port_cfg->field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
        p_ipfix_port_cfg->tcp_end_detect_disable = GetDsIpfixIngPortCfg(V, tcpSessionEndDetectDisable_f, &ipfix_ing_cfg);
        p_ipfix_port_cfg->learn_disable = GetDsIpfixIngPortCfg(V, portSessionNumLimitEn_f, &ipfix_ing_cfg);
        flow_type = GetDsIpfixIngPortCfg(V, flowType_f, &ipfix_ing_cfg);
        if(0 == flow_type)
        {
            p_ipfix_port_cfg->flow_type = CTC_IPFIX_FLOW_TYPE_ALL_PACKET;
        }
        else if(1 == flow_type)
        {
            p_ipfix_port_cfg->flow_type = CTC_IPFIX_FLOW_TYPE_NON_DISCARD_PACKET;
        }
        else if(2 == flow_type)
        {
            p_ipfix_port_cfg->flow_type = CTC_IPFIX_FLOW_TYPE_DISCARD_PACKET;
        }

        samping_idx = GetDsIpfixIngPortCfg(V, samplingProfileIndex_f, &ipfix_ing_cfg);
        if(GetDsIpfixIngPortCfg(V, samplingEn_f, &ipfix_ing_cfg))
        {
            if (samping_idx == 0)
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixIngSamplingProfile(V, array_0_ingSamplingPktInterval_f, &ipfix_ing_sampleing_cfg);
            }
            else if(samping_idx == 1)
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixIngSamplingProfile(V, array_1_ingSamplingPktInterval_f, &ipfix_ing_sampleing_cfg);
            }
            else if(samping_idx == 2)
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixIngSamplingProfile(V, array_2_ingSamplingPktInterval_f, &ipfix_ing_sampleing_cfg);
            }
            else
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixIngSamplingProfile(V, array_3_ingSamplingPktInterval_f, &ipfix_ing_sampleing_cfg);
            }
        }
        else
        {
            p_ipfix_port_cfg->sample_interval = 0;
        }
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port), p_gg_ipfix_master[lchip]->mutex);

        cmd = DRV_IOR( IpfixEgrSamplingProfile_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &ipfix_egr_sampleing_cfg), p_gg_ipfix_master[lchip]->mutex);

        cmd = DRV_IOR( DsIpfixEgrPortCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, lport, cmd, &ipfix_egr_cfg), p_gg_ipfix_master[lchip]->mutex);

        p_ipfix_port_cfg->lkup_type = GetDsDestPort(V, portFlowHashType_f, &ds_dest_port);
        p_ipfix_port_cfg->field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
        p_ipfix_port_cfg->tcp_end_detect_disable = GetDsIpfixEgrPortCfg(V, tcpSessionEndDetectDisable_f, &ipfix_egr_cfg);
        p_ipfix_port_cfg->learn_disable = GetDsIpfixEgrPortCfg(V, portSessionNumLimitEn_f, &ipfix_egr_cfg);
        p_ipfix_port_cfg->flow_type = GetDsIpfixEgrPortCfg(V, flowType_f, &ipfix_egr_cfg);
        samping_idx = GetDsIpfixEgrPortCfg(V, samplingProfileIndex_f, &ipfix_egr_cfg);
        if(GetDsIpfixEgrPortCfg(V, samplingEn_f, &ipfix_egr_cfg))
        {
            if (samping_idx == 0)
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixEgrSamplingProfile(V, array_0_egrSamplingPktInterval_f, &ipfix_egr_sampleing_cfg);
            }
            else if(samping_idx == 1)
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixEgrSamplingProfile(V, array_1_egrSamplingPktInterval_f, &ipfix_egr_sampleing_cfg);
            }
            else if(samping_idx == 2)
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixEgrSamplingProfile(V, array_2_egrSamplingPktInterval_f, &ipfix_egr_sampleing_cfg);
            }
            else
            {
                 p_ipfix_port_cfg->sample_interval = GetIpfixEgrSamplingProfile(V, array_3_egrSamplingPktInterval_f, &ipfix_egr_sampleing_cfg);
            }
        }
        else
        {
            p_ipfix_port_cfg->sample_interval = 0;
        }
    }

    SYS_IPFIX_UNLOCK(lchip);
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_set_l2_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel)
{

  IpfixL2HashFieldSelect_m  hash_field;
  uint32 cmd = 0;

  sal_memset(&hash_field, 0, sizeof(IpfixL2HashFieldSelect_m));

  SetIpfixL2HashFieldSelect(V, cfiEn_f, &hash_field, field_sel->u.mac.cfi);
  SetIpfixL2HashFieldSelect(V, cosEn_f, &hash_field, field_sel->u.mac.cos);
  SetIpfixL2HashFieldSelect(V, vlanIdEn_f, &hash_field, field_sel->u.mac.vlan_id);
  SetIpfixL2HashFieldSelect(V, etherTypeEn_f, &hash_field, field_sel->u.mac.eth_type);
  SetIpfixL2HashFieldSelect(V, globalSrcPortEn_f, &hash_field, field_sel->u.mac.gport);
  SetIpfixL2HashFieldSelect(V, isMergeKeyEn_f, &hash_field, 0);
  SetIpfixL2HashFieldSelect(V, logicSrcPortEn_f, &hash_field, field_sel->u.mac.logic_port);
  SetIpfixL2HashFieldSelect(V, macDaEn_f, &hash_field, field_sel->u.mac.mac_da);
  SetIpfixL2HashFieldSelect(V, macSaEn_f, &hash_field, field_sel->u.mac.mac_sa);
  SetIpfixL2HashFieldSelect(V, metadataEn_f, &hash_field, field_sel->u.mac.metadata);
  if(field_sel->u.mac.vlan_id || field_sel->u.mac.cfi || field_sel->u.mac.cos)
  {
    SetIpfixL2HashFieldSelect(V, vlanIdValidEn_f, &hash_field, 1);
  }
  SetIpfixL2HashFieldSelect(V, isMergeKeyEn_f, &hash_field, 0);
  cmd = DRV_IOW( IpfixL2HashFieldSelect_t, DRV_ENTRY_FLAG);
  CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &hash_field));

  return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_set_ipv4_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel)
{

    IpfixL3Ipv4HashFieldSelect_m  hash_field;
    uint32 cmd = 0;
    uint32 mask_len = 0;
    uint8 is_gre = !!field_sel->u.ipv4.gre_key;
    uint8 is_icmp = field_sel->u.ipv4.icmp_code || field_sel->u.ipv4.icmp_type;
    uint8 is_vxlan = !!field_sel->u.ipv4.vxlan_vni;
    uint8 is_l4_port = field_sel->u.ipv4.l4_src_port || field_sel->u.ipv4.l4_dst_port;

    sal_memset(&hash_field, 0, sizeof(IpfixL3Ipv4HashFieldSelect_m));

    /* if you want to do ipfix by vxlan_vin, you must cfg l4_sub_type meanwhile*/
    if ((field_sel->u.ipv4.vxlan_vni == 1) && (field_sel->u.ipv4.l4_sub_type == 0))
    {
        return CTC_E_INVALID_PARAM;
    }
    /* if you want to do ipfix by l4 info, you must cfg l4_type meanwhile */
    if (((field_sel->u.ipv4.gre_key == 1) || (field_sel->u.ipv4.icmp_code == 1)
        || (field_sel->u.ipv4.icmp_type == 1) || (field_sel->u.ipv4.l4_sub_type == 1)
        || (field_sel->u.ipv4.l4_dst_port == 1) || (field_sel->u.ipv4.l4_src_port == 1))
        && (field_sel->u.ipv4.ip_protocol== 0))
    {
        return CTC_E_INVALID_PARAM;
    }

    if((is_gre + is_icmp + is_vxlan + is_l4_port) > 1 )
    {
        return CTC_E_PARAM_CONFLICT;
    }

    SetIpfixL3Ipv4HashFieldSelect(V, dscpEn_f, &hash_field, field_sel->u.ipv4.dscp);
    SetIpfixL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f, &hash_field, field_sel->u.ipv4.icmp_code);
    SetIpfixL3Ipv4HashFieldSelect(V, icmpTypeEn_f, &hash_field, field_sel->u.ipv4.icmp_type);
    SetIpfixL3Ipv4HashFieldSelect(V, globalSrcPortEn_f, &hash_field, field_sel->u.ipv4.gport);
    SetIpfixL3Ipv4HashFieldSelect(V, greKeyEn_f, &hash_field, field_sel->u.ipv4.gre_key);
    SetIpfixL3Ipv4HashFieldSelect(V, logicSrcPortEn_f, &hash_field, field_sel->u.ipv4.logic_port);
    SetIpfixL3Ipv4HashFieldSelect(V, ignorVxlan_f, &hash_field, (field_sel->u.ipv4.vxlan_vni)?0:1);
    SetIpfixL3Ipv4HashFieldSelect(V, isMergeKeyEn_f, &hash_field, 1);

    if (field_sel->u.ipv4.ip_da)
    {
        IPFIX_IPV4_MASK_LEN_CHECK(field_sel->u.ipv4.ip_da_mask);
        mask_len = field_sel->u.ipv4.ip_da_mask - 1;

        SetIpfixL3Ipv4HashFieldSelect(V,  ipDaEn_f, &hash_field, mask_len);
    }

    if (field_sel->u.ipv4.ip_sa)
    {
        IPFIX_IPV4_MASK_LEN_CHECK(field_sel->u.ipv4.ip_sa_mask);
        mask_len = field_sel->u.ipv4.ip_sa_mask - 1;

        SetIpfixL3Ipv4HashFieldSelect(V,  ipSaEn_f, &hash_field, mask_len);
    }

    SetIpfixL3Ipv4HashFieldSelect(V, isRouteMacEn_f, &hash_field, 0);
    SetIpfixL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &hash_field, field_sel->u.ipv4.vxlan_vni);
    SetIpfixL3Ipv4HashFieldSelect(V, l4DestPortEn_f, &hash_field, field_sel->u.ipv4.l4_dst_port);
    SetIpfixL3Ipv4HashFieldSelect(V, l4SourcePortEn_f, &hash_field, field_sel->u.ipv4.l4_src_port);
    SetIpfixL3Ipv4HashFieldSelect(V, metadataEn_f, &hash_field, field_sel->u.ipv4.metadata);
    SetIpfixL3Ipv4HashFieldSelect(V, layer3HeaderProtocolEn_f, &hash_field, field_sel->u.ipv4.ip_protocol);
    SetIpfixL3Ipv4HashFieldSelect(V, layer4UserTypeEn_f , &hash_field, field_sel->u.ipv4.l4_sub_type);

    cmd = DRV_IOW( IpfixL3Ipv4HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &hash_field));

  return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_set_ipv6_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel)
{

    IpfixL3Ipv6HashFieldSelect_m  hash_field;
    uint32 cmd = 0;
    uint32 mask_len = 0;
    uint8 is_gre = !!field_sel->u.ipv6.gre_key;
    uint8 is_icmp = field_sel->u.ipv6.icmp_code || field_sel->u.ipv6.icmp_type;
    uint8 is_vxlan = !!field_sel->u.ipv6.vxlan_vni;
    uint8 is_l4_port = field_sel->u.ipv6.l4_src_port || field_sel->u.ipv6.l4_dst_port;

   sal_memset(&hash_field, 0, sizeof(IpfixL3Ipv6HashFieldSelect_m));

    /* if you want to do ipfix by vxlan_vin, you must cfg l4_sub_type meanwhile*/
    if ((field_sel->u.ipv6.vxlan_vni == 1) && (field_sel->u.ipv6.l4_sub_type == 0))
    {
        return CTC_E_INVALID_PARAM;
    }
    /* if you want to do ipfix by l4 info, you must cfg l4_type meanwhile */
    if (((field_sel->u.ipv6.gre_key == 1) || (field_sel->u.ipv6.icmp_code == 1)
        || (field_sel->u.ipv6.icmp_type == 1) || (field_sel->u.ipv6.l4_sub_type == 1)
        || (field_sel->u.ipv6.l4_dst_port == 1) || (field_sel->u.ipv6.l4_src_port == 1))
        && (field_sel->u.ipv6.l4_type == 0))
    {
        return CTC_E_INVALID_PARAM;
    }

    if((is_gre + is_icmp + is_vxlan + is_l4_port) > 1 )
    {
        return CTC_E_PARAM_CONFLICT;
    }

    SetIpfixL3Ipv6HashFieldSelect(V, dscpEn_f, &hash_field, field_sel->u.ipv6.dscp);
    SetIpfixL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f, &hash_field, field_sel->u.ipv6.icmp_code);
    SetIpfixL3Ipv6HashFieldSelect(V, icmpTypeEn_f, &hash_field, field_sel->u.ipv6.icmp_type);
    SetIpfixL3Ipv6HashFieldSelect(V, globalSrcPortEn_f, &hash_field, field_sel->u.ipv6.gport);
    SetIpfixL3Ipv6HashFieldSelect(V, greKeyEn_f, &hash_field, field_sel->u.ipv6.gre_key);
    SetIpfixL3Ipv6HashFieldSelect(V, logicSrcPortEn_f, &hash_field, field_sel->u.ipv6.logic_port);
    SetIpfixL3Ipv6HashFieldSelect(V, ignorVxlan_f, &hash_field, (field_sel->u.ipv6.vxlan_vni)?0:1);
    SetIpfixL3Ipv6HashFieldSelect(V, isMergeKeyEn_f, &hash_field, 1);

    if (field_sel->u.ipv6.ip_da)
    {
        IPFIX_IPV6_MASK_LEN_CHECK(field_sel->u.ipv6.ip_da_mask);
        mask_len = (field_sel->u.ipv6.ip_da_mask>>2)-1;

        SetIpfixL3Ipv6HashFieldSelect(V, ipDaEn_f, &hash_field, mask_len);
    }

    if (field_sel->u.ipv6.ip_sa)
    {
        IPFIX_IPV6_MASK_LEN_CHECK(field_sel->u.ipv6.ip_sa_mask);
        mask_len = (field_sel->u.ipv6.ip_sa_mask>>2)-1;
        SetIpfixL3Ipv6HashFieldSelect(V, ipSaEn_f, &hash_field, mask_len);
    }

    SetIpfixL3Ipv6HashFieldSelect(V, ipv6FlowLabelEn_f, &hash_field, field_sel->u.ipv6.flow_label);
    SetIpfixL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &hash_field, field_sel->u.ipv6.vxlan_vni);
    SetIpfixL3Ipv6HashFieldSelect(V, layer4TypeEn_f, &hash_field, field_sel->u.ipv6.l4_type);
    SetIpfixL3Ipv6HashFieldSelect(V, isVxlanEn_f, &hash_field, field_sel->u.ipv6.l4_sub_type);
    SetIpfixL3Ipv6HashFieldSelect(V, l4DestPortEn_f, &hash_field, field_sel->u.ipv6.l4_dst_port);
    SetIpfixL3Ipv6HashFieldSelect(V, l4SourcePortEn_f, &hash_field, field_sel->u.ipv6.l4_src_port);
    SetIpfixL3Ipv6HashFieldSelect(V, metadataEn_f, &hash_field, field_sel->u.ipv6.metadata);

    cmd = DRV_IOW( IpfixL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &hash_field));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_set_mpls_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel)
{

  IpfixL3MplsHashFieldSelect_m  hash_field;
  uint32 cmd = 0;

   sal_memset(&hash_field, 0, sizeof(IpfixL3MplsHashFieldSelect_m));

   SetIpfixL3MplsHashFieldSelect(V,  labelNumEn_f      ,&hash_field, field_sel->u.mpls.label_num);
   SetIpfixL3MplsHashFieldSelect(V,  globalSrcPortEn_f,   &hash_field,field_sel->u.mpls.gport);
   SetIpfixL3MplsHashFieldSelect(V,  logicSrcPortEn_f  ,&hash_field,field_sel->u.mpls.logic_port);
   SetIpfixL3MplsHashFieldSelect(V,  metadataEn_f      ,&hash_field, field_sel->u.mpls.metadata);
   SetIpfixL3MplsHashFieldSelect(V,  mplsExp0En_f      ,&hash_field,field_sel->u.mpls.mpls_label0_exp);
   SetIpfixL3MplsHashFieldSelect(V,  mplsExp1En_f      ,&hash_field,field_sel->u.mpls.mpls_label1_exp);
   SetIpfixL3MplsHashFieldSelect(V,  mplsExp2En_f      ,&hash_field, field_sel->u.mpls.mpls_label2_exp);
   SetIpfixL3MplsHashFieldSelect(V, mplsLabel0En_f    , &hash_field,field_sel->u.mpls.mpls_label0_label);
   SetIpfixL3MplsHashFieldSelect(V, mplsLabel1En_f    , &hash_field,field_sel->u.mpls.mpls_label1_label);
   SetIpfixL3MplsHashFieldSelect(V, mplsLabel2En_f    , &hash_field,field_sel->u.mpls.mpls_label2_label);
   SetIpfixL3MplsHashFieldSelect(V, mplsSbit0En_f     , &hash_field,field_sel->u.mpls.mpls_label0_s);
   SetIpfixL3MplsHashFieldSelect(V, mplsSbit1En_f     , &hash_field,field_sel->u.mpls.mpls_label1_s);
   SetIpfixL3MplsHashFieldSelect(V, mplsSbit2En_f     , &hash_field,field_sel->u.mpls.mpls_label2_s);
   SetIpfixL3MplsHashFieldSelect(V, mplsTtl0En_f      , &hash_field,field_sel->u.mpls.mpls_label0_ttl);
   SetIpfixL3MplsHashFieldSelect(V, mplsTtl1En_f      , &hash_field,field_sel->u.mpls.mpls_label1_ttl);
   SetIpfixL3MplsHashFieldSelect(V, mplsTtl2En_f      , &hash_field,field_sel->u.mpls.mpls_label2_ttl);

  cmd = DRV_IOW( IpfixL3MplsHashFieldSelect_t, DRV_ENTRY_FLAG);
  CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &hash_field));
  return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_set_l2l3_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel)
{

    IpfixL2L3HashFieldSelect_m  hash_field;
    uint32 cmd = 0;
    uint32 mask_len = 0;
    uint8 is_gre = !!field_sel->u.l2_l3.gre_key;
    uint8 is_icmp = field_sel->u.l2_l3.icmp_code || field_sel->u.l2_l3.icmp_type;
    uint8 is_vxlan = !!field_sel->u.l2_l3.vxlan_vni;
    uint8 is_l4_port = field_sel->u.l2_l3.l4_src_port || field_sel->u.l2_l3.l4_dst_port;

   sal_memset(&hash_field, 0, sizeof(IpfixL2L3HashFieldSelect_m));

    /* if you want to do ipfix by vxlan_vin, you must cfg l4_sub_type meanwhile*/
    if ((field_sel->u.l2_l3.vxlan_vni == 1) && (field_sel->u.l2_l3.l4_sub_type == 0))
    {
        return CTC_E_INVALID_PARAM;
    }
    /* if you want to do ipfix by l4 info, you must cfg l4_type meanwhile */
    if (((field_sel->u.l2_l3.gre_key == 1) || (field_sel->u.l2_l3.icmp_code == 1)
        || (field_sel->u.l2_l3.icmp_type == 1) || (field_sel->u.l2_l3.l4_sub_type == 1)
        || (field_sel->u.l2_l3.l4_dst_port == 1) || (field_sel->u.l2_l3.l4_src_port == 1))
        && (field_sel->u.l2_l3.l4_type == 0))
    {
        return CTC_E_INVALID_PARAM;
    }

    if((is_gre + is_icmp + is_vxlan + is_l4_port) > 1 )
    {
        return CTC_E_PARAM_CONFLICT;
    }

    SetIpfixL2L3HashFieldSelect(V,  ctagCfiEn_f       , &hash_field,field_sel->u.l2_l3.ctag_cfi);
    SetIpfixL2L3HashFieldSelect(V,  ctagCosEn_f       , &hash_field,field_sel->u.l2_l3.ctag_cos);
    SetIpfixL2L3HashFieldSelect(V,  cvlanIdEn_f       , &hash_field,field_sel->u.l2_l3.ctag_vlan);
    if (field_sel->u.l2_l3.ctag_cfi || field_sel->u.l2_l3.ctag_cos || field_sel->u.l2_l3.ctag_vlan)
    {
        SetIpfixL2L3HashFieldSelect(V,  cvlanIdValidEn_f  , &hash_field,1);
    }

    SetIpfixL2L3HashFieldSelect(V,  stagCfiEn_f       , &hash_field,field_sel->u.l2_l3.stag_cfi);
    SetIpfixL2L3HashFieldSelect(V,  stagCosEn_f       , &hash_field,field_sel->u.l2_l3.stag_cos);
    SetIpfixL2L3HashFieldSelect(V,  svlanIdEn_f       , &hash_field,field_sel->u.l2_l3.stag_vlan);
    if (field_sel->u.l2_l3.stag_cfi || field_sel->u.l2_l3.stag_cos || field_sel->u.l2_l3.stag_vlan)
    {
        SetIpfixL2L3HashFieldSelect(V,  svlanIdValidEn_f  , &hash_field,1);
    }


    SetIpfixL2L3HashFieldSelect(V,  dscpEn_f          , &hash_field,field_sel->u.l2_l3.dscp);
    SetIpfixL2L3HashFieldSelect(V,  ecnEn_f           , &hash_field,field_sel->u.l2_l3.ecn);
    SetIpfixL2L3HashFieldSelect(V,  etherTypeEn_f     , &hash_field,field_sel->u.l2_l3.eth_type);
    SetIpfixL2L3HashFieldSelect(V,  globalSrcPortEn_f , &hash_field,field_sel->u.l2_l3.gport);
    SetIpfixL2L3HashFieldSelect(V,  greKeyEn_f        , &hash_field, field_sel->u.l2_l3.gre_key);
    SetIpfixL2L3HashFieldSelect(V,  icmpOpcodeEn_f    , &hash_field,field_sel->u.l2_l3.icmp_code);
    SetIpfixL2L3HashFieldSelect(V,  icmpTypeEn_f      , &hash_field,field_sel->u.l2_l3.icmp_type);
    SetIpfixL2L3HashFieldSelect(V,  ignorVxlan_f      , &hash_field, 0);
    SetIpfixL2L3HashFieldSelect(V,  isMergeKeyEn_f, &hash_field, 1);

    if (field_sel->u.l2_l3.ip_da)
    {
        IPFIX_IPV4_MASK_LEN_CHECK(field_sel->u.l2_l3.ip_da_mask);
        mask_len = field_sel->u.l2_l3.ip_da_mask - 1;

        SetIpfixL2L3HashFieldSelect(V,  ipDaEn_f, &hash_field, mask_len);
    }

    if (field_sel->u.l2_l3.ip_sa)
    {
        IPFIX_IPV4_MASK_LEN_CHECK(field_sel->u.l2_l3.ip_sa_mask);
        mask_len = field_sel->u.l2_l3.ip_sa_mask - 1;

       SetIpfixL2L3HashFieldSelect(V,  ipSaEn_f, &hash_field, mask_len);
    }

   SetIpfixL2L3HashFieldSelect(V,  vrfIdEn_f, &hash_field,field_sel->u.l2_l3.vrfid);
   SetIpfixL2L3HashFieldSelect(V,  l4DestPortEn_f, &hash_field,field_sel->u.l2_l3.l4_dst_port);
   SetIpfixL2L3HashFieldSelect(V,  l4InfoMappedEn_f, &hash_field,0);
   SetIpfixL2L3HashFieldSelect(V,  l4SourcePortEn_f, &hash_field,field_sel->u.l2_l3.l4_src_port);
   SetIpfixL2L3HashFieldSelect(V,  labelNumEn_f, &hash_field,field_sel->u.l2_l3.label_num);
   SetIpfixL2L3HashFieldSelect(V,  layer4TypeEn_f, &hash_field,field_sel->u.l2_l3.l4_type);
   SetIpfixL2L3HashFieldSelect(V,  layer4UserTypeEn_f, &hash_field,field_sel->u.l2_l3.l4_sub_type);
   SetIpfixL2L3HashFieldSelect(V,  logicSrcPortEn_f  , &hash_field,field_sel->u.l2_l3.logic_port);
   SetIpfixL2L3HashFieldSelect(V,  macDaEn_f         , &hash_field,field_sel->u.l2_l3.mac_da);
   SetIpfixL2L3HashFieldSelect(V,  macSaEn_f         , &hash_field,field_sel->u.l2_l3.mac_sa);
   SetIpfixL2L3HashFieldSelect(V,  metadataEn_f      , &hash_field,field_sel->u.l2_l3.metadata);
   SetIpfixL2L3HashFieldSelect(V,  mplsExp0En_f      , &hash_field,field_sel->u.l2_l3.mpls_label0_exp);
   SetIpfixL2L3HashFieldSelect(V,  mplsExp1En_f      , &hash_field,field_sel->u.l2_l3.mpls_label1_exp);
   SetIpfixL2L3HashFieldSelect(V,  mplsExp2En_f      , &hash_field,field_sel->u.l2_l3.mpls_label2_exp);
   SetIpfixL2L3HashFieldSelect(V, mplsLabel0En_f    , &hash_field,field_sel->u.l2_l3.mpls_label0_label);
   SetIpfixL2L3HashFieldSelect(V, mplsLabel1En_f    , &hash_field,field_sel->u.l2_l3.mpls_label1_label);
   SetIpfixL2L3HashFieldSelect(V, mplsLabel2En_f    , &hash_field,field_sel->u.l2_l3.mpls_label2_label);
   SetIpfixL2L3HashFieldSelect(V, mplsSbit0En_f     , &hash_field,field_sel->u.l2_l3.mpls_label0_s);
   SetIpfixL2L3HashFieldSelect(V, mplsSbit1En_f     , &hash_field,field_sel->u.l2_l3.mpls_label1_s);
   SetIpfixL2L3HashFieldSelect(V, mplsSbit2En_f     , &hash_field,field_sel->u.l2_l3.mpls_label2_s);
   SetIpfixL2L3HashFieldSelect(V, mplsTtl0En_f      , &hash_field,field_sel->u.l2_l3.mpls_label0_ttl);
   SetIpfixL2L3HashFieldSelect(V, mplsTtl1En_f      , &hash_field,field_sel->u.l2_l3.mpls_label1_ttl);
   SetIpfixL2L3HashFieldSelect(V, mplsTtl2En_f      , &hash_field,field_sel->u.l2_l3.mpls_label2_ttl);
   SetIpfixL2L3HashFieldSelect(V, ttlEn_f           , &hash_field,field_sel->u.l2_l3.ttl);
   SetIpfixL2L3HashFieldSelect(V,  vxlanVniEn_f    , &hash_field,field_sel->u.l2_l3.vxlan_vni);

  cmd = DRV_IOW( IpfixL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
  CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel->field_sel_id, cmd, &hash_field));
  return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_set_hash_field_sel(uint8 lchip, ctc_ipfix_hash_field_sel_t* field_sel)
{
    SYS_IPFIX_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(field_sel);
    CTC_MAX_VALUE_CHECK(field_sel->field_sel_id, 15);

    SYS_IPFIX_LOCK(lchip);
    switch(field_sel->key_type)
    {
    case CTC_IPFIX_KEY_HASH_MAC:
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_ipfix_set_l2_hash_field_sel(lchip, field_sel), p_gg_ipfix_master[lchip]->mutex);
        break;
    case CTC_IPFIX_KEY_HASH_IPV4:
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_ipfix_set_ipv4_hash_field_sel(lchip, field_sel), p_gg_ipfix_master[lchip]->mutex);
        break;
    case CTC_IPFIX_KEY_HASH_MPLS:
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_ipfix_set_mpls_hash_field_sel(lchip, field_sel), p_gg_ipfix_master[lchip]->mutex);
        break;
    case CTC_IPFIX_KEY_HASH_IPV6:
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_ipfix_set_ipv6_hash_field_sel(lchip, field_sel), p_gg_ipfix_master[lchip]->mutex);
        break;
    case CTC_IPFIX_KEY_HASH_L2_L3:
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_ipfix_set_l2l3_hash_field_sel(lchip, field_sel), p_gg_ipfix_master[lchip]->mutex);
        break;
    default:
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    SYS_IPFIX_UNLOCK(lchip);
    return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_get_hash_field_sel(uint8 lchip, uint32 field_sel_id, uint8 key_type, ctc_ipfix_hash_field_sel_t* field_sel)
{
    uint32 cmd = 0;
    IpfixL2HashFieldSelect_m  l2_hash_field;
    IpfixL3Ipv4HashFieldSelect_m  v4_hash_field;
    IpfixL3Ipv6HashFieldSelect_m  v6_hash_field;
    IpfixL3MplsHashFieldSelect_m  mpls_hash_field;
    IpfixL2L3HashFieldSelect_m  l2l3_hash_field;

    SYS_IPFIX_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(field_sel);
    CTC_MAX_VALUE_CHECK(field_sel_id, 15);

    field_sel->field_sel_id = field_sel_id;


    switch(key_type)
    {
        case CTC_IPFIX_KEY_HASH_MAC:
            cmd = DRV_IOR( IpfixL2HashFieldSelect_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &l2_hash_field));
            field_sel->u.mac.cfi = GetIpfixL2HashFieldSelect(V, cfiEn_f, &l2_hash_field);
            field_sel->u.mac.cos = GetIpfixL2HashFieldSelect(V, cosEn_f, &l2_hash_field);
            field_sel->u.mac.eth_type = GetIpfixL2HashFieldSelect(V, etherTypeEn_f, &l2_hash_field);
            field_sel->u.mac.gport = GetIpfixL2HashFieldSelect(V, globalSrcPortEn_f, &l2_hash_field);
            field_sel->u.mac.logic_port = GetIpfixL2HashFieldSelect(V, logicSrcPortEn_f, &l2_hash_field);
            field_sel->u.mac.mac_da = GetIpfixL2HashFieldSelect(V, macDaEn_f, &l2_hash_field);
            field_sel->u.mac.mac_sa = GetIpfixL2HashFieldSelect(V, macSaEn_f, &l2_hash_field);
            field_sel->u.mac.metadata = GetIpfixL2HashFieldSelect(V, metadataEn_f, &l2_hash_field);
            field_sel->u.mac.vlan_id = GetIpfixL2HashFieldSelect(V, vlanIdEn_f, &l2_hash_field);
 /*          field_sel->u.mac.vni = GetIpfixL2HashFieldSelect(V, vniEn_f, &l2_hash_field);*/
 /*          field_sel->u.mac.merge = GetIpfixL2HashFieldSelect(V, isMergeKeyEn_f, &l2_hash_field);*/
            break;

        case CTC_IPFIX_KEY_HASH_IPV4:
            cmd = DRV_IOR( IpfixL3Ipv4HashFieldSelect_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &v4_hash_field));
            field_sel->u.ipv4.dscp = GetIpfixL3Ipv4HashFieldSelect(V, dscpEn_f, &v4_hash_field);
            field_sel->u.ipv4.icmp_code = GetIpfixL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f, &v4_hash_field);
            field_sel->u.ipv4.icmp_type = GetIpfixL3Ipv4HashFieldSelect(V, icmpTypeEn_f, &v4_hash_field);
            field_sel->u.ipv4.gport = GetIpfixL3Ipv4HashFieldSelect(V, globalSrcPortEn_f, &v4_hash_field);
            field_sel->u.ipv4.gre_key = GetIpfixL3Ipv4HashFieldSelect(V, greKeyEn_f, &v4_hash_field);
            field_sel->u.ipv4.logic_port = GetIpfixL3Ipv4HashFieldSelect(V, logicSrcPortEn_f, &v4_hash_field);
 /*          field_sel->u.ipv4.merge = GetIpfixL3Ipv4HashFieldSelect(V, isMergeKeyEn_f, &v4_hash_field);*/
            field_sel->u.ipv4.ip_da_mask = GetIpfixL3Ipv4HashFieldSelect(V,  ipDaEn_f, &v4_hash_field);
            if (0 != field_sel->u.ipv4.ip_da_mask)
            {
                field_sel->u.ipv4.ip_da_mask = field_sel->u.ipv4.ip_da_mask + 1;
            }
            field_sel->u.ipv4.ip_da = field_sel->u.ipv4.ip_da_mask?1:0;
            field_sel->u.ipv4.ip_sa_mask = GetIpfixL3Ipv4HashFieldSelect(V,  ipSaEn_f, &v4_hash_field);
            if (0 != field_sel->u.ipv4.ip_sa_mask)
            {
                field_sel->u.ipv4.ip_sa_mask = field_sel->u.ipv4.ip_sa_mask + 1;
            }
            field_sel->u.ipv4.ip_sa = field_sel->u.ipv4.ip_sa_mask?1:0;
            field_sel->u.ipv4.vxlan_vni = GetIpfixL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &v4_hash_field);
            field_sel->u.ipv4.l4_dst_port = GetIpfixL3Ipv4HashFieldSelect(V, l4DestPortEn_f, &v4_hash_field);
            field_sel->u.ipv4.l4_src_port = GetIpfixL3Ipv4HashFieldSelect(V, l4SourcePortEn_f, &v4_hash_field);
            field_sel->u.ipv4.metadata = GetIpfixL3Ipv4HashFieldSelect(V, metadataEn_f, &v4_hash_field);
            field_sel->u.ipv4.ip_protocol= GetIpfixL3Ipv4HashFieldSelect(V, layer3HeaderProtocolEn_f, &v4_hash_field);
            break;

        case CTC_IPFIX_KEY_HASH_MPLS:
            cmd = DRV_IOR( IpfixL3MplsHashFieldSelect_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &mpls_hash_field));
            field_sel->u.mpls.label_num = GetIpfixL3MplsHashFieldSelect(V,  labelNumEn_f, &mpls_hash_field);
            field_sel->u.mpls.gport = GetIpfixL3MplsHashFieldSelect(V,  globalSrcPortEn_f,   &mpls_hash_field);
            field_sel->u.mpls.logic_port = GetIpfixL3MplsHashFieldSelect(V,  logicSrcPortEn_f, &mpls_hash_field);
            field_sel->u.mpls.metadata = GetIpfixL3MplsHashFieldSelect(V,  metadataEn_f, &mpls_hash_field);
            field_sel->u.mpls.mpls_label0_exp = GetIpfixL3MplsHashFieldSelect(V,  mplsExp0En_f, &mpls_hash_field);
            field_sel->u.mpls.mpls_label1_exp = GetIpfixL3MplsHashFieldSelect(V,  mplsExp1En_f      ,&mpls_hash_field);
            field_sel->u.mpls.mpls_label2_exp = GetIpfixL3MplsHashFieldSelect(V,  mplsExp2En_f      ,&mpls_hash_field);
            field_sel->u.mpls.mpls_label0_label = GetIpfixL3MplsHashFieldSelect(V, mplsLabel0En_f    , &mpls_hash_field);
            field_sel->u.mpls.mpls_label1_label = GetIpfixL3MplsHashFieldSelect(V, mplsLabel1En_f    , &mpls_hash_field);
            field_sel->u.mpls.mpls_label2_label = GetIpfixL3MplsHashFieldSelect(V, mplsLabel2En_f    , &mpls_hash_field);
            field_sel->u.mpls.mpls_label0_s = GetIpfixL3MplsHashFieldSelect(V, mplsSbit0En_f     , &mpls_hash_field);
            field_sel->u.mpls.mpls_label1_s = GetIpfixL3MplsHashFieldSelect(V, mplsSbit1En_f     , &mpls_hash_field);
            field_sel->u.mpls.mpls_label2_s = GetIpfixL3MplsHashFieldSelect(V, mplsSbit2En_f     , &mpls_hash_field);
            field_sel->u.mpls.mpls_label0_ttl = GetIpfixL3MplsHashFieldSelect(V, mplsTtl0En_f      , &mpls_hash_field);
            field_sel->u.mpls.mpls_label1_ttl = GetIpfixL3MplsHashFieldSelect(V, mplsTtl1En_f      , &mpls_hash_field);
            field_sel->u.mpls.mpls_label2_ttl = GetIpfixL3MplsHashFieldSelect(V, mplsTtl2En_f      , &mpls_hash_field);

            break;
        case CTC_IPFIX_KEY_HASH_IPV6:
            cmd = DRV_IOR( IpfixL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &v6_hash_field));
            field_sel->u.ipv6.dscp = GetIpfixL3Ipv6HashFieldSelect(V, dscpEn_f, &v6_hash_field);
            field_sel->u.ipv6.icmp_code = GetIpfixL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f, &v6_hash_field);
            field_sel->u.ipv6.icmp_type = GetIpfixL3Ipv6HashFieldSelect(V, icmpTypeEn_f, &v6_hash_field);
            field_sel->u.ipv6.gport = GetIpfixL3Ipv6HashFieldSelect(V, globalSrcPortEn_f, &v6_hash_field);
            field_sel->u.ipv6.gre_key = GetIpfixL3Ipv6HashFieldSelect(V, greKeyEn_f, &v6_hash_field);
            field_sel->u.ipv6.logic_port = GetIpfixL3Ipv6HashFieldSelect(V, logicSrcPortEn_f, &v6_hash_field);
 /*          field_sel->u.ipv6.merge = GetIpfixL3Ipv6HashFieldSelect(V, isMergeKeyEn_f, &v6_hash_field);*/
            field_sel->u.ipv6.ip_da_mask = GetIpfixL3Ipv6HashFieldSelect(V, ipDaEn_f, &v6_hash_field);
            if (0 != field_sel->u.ipv6.ip_da_mask)
            {
                field_sel->u.ipv6.ip_da_mask = ((field_sel->u.ipv6.ip_da_mask + 1) << 2);
            }
            field_sel->u.ipv6.ip_sa_mask = GetIpfixL3Ipv6HashFieldSelect(V, ipSaEn_f, &v6_hash_field);
            if (0 != field_sel->u.ipv6.ip_sa_mask)
            {
                field_sel->u.ipv6.ip_sa_mask = ((field_sel->u.ipv6.ip_sa_mask + 1) << 2);
            }
            field_sel->u.ipv6.flow_label = GetIpfixL3Ipv6HashFieldSelect(V, ipv6FlowLabelEn_f, &v6_hash_field);
            field_sel->u.ipv6.vxlan_vni = GetIpfixL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &v6_hash_field);
            field_sel->u.ipv6.l4_dst_port = GetIpfixL3Ipv6HashFieldSelect(V, l4DestPortEn_f, &v6_hash_field);
            field_sel->u.ipv6.l4_src_port = GetIpfixL3Ipv6HashFieldSelect(V, l4SourcePortEn_f, &v6_hash_field);
            field_sel->u.ipv6.metadata = GetIpfixL3Ipv6HashFieldSelect(V, metadataEn_f, &v6_hash_field);

            break;
        case CTC_IPFIX_KEY_HASH_L2_L3:
            cmd = DRV_IOR( IpfixL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &l2l3_hash_field));
            field_sel->u.l2_l3.ctag_cfi= GetIpfixL2L3HashFieldSelect(V,  ctagCfiEn_f       , &l2l3_hash_field);
            field_sel->u.l2_l3.ctag_cos = GetIpfixL2L3HashFieldSelect(V,  ctagCosEn_f       , &l2l3_hash_field);
            field_sel->u.l2_l3.ctag_vlan= GetIpfixL2L3HashFieldSelect(V,  cvlanIdEn_f       , &l2l3_hash_field);
             /*field_sel->u.l2_l3.ctag_valid= GetIpfixL2L3HashFieldSelect(V,  cvlanIdValidEn_f  , &l2l3_hash_field);*/
            field_sel->u.l2_l3.dscp= GetIpfixL2L3HashFieldSelect(V,  dscpEn_f          , &l2l3_hash_field);
            field_sel->u.l2_l3.ecn= GetIpfixL2L3HashFieldSelect(V,  ecnEn_f           , &l2l3_hash_field);
            field_sel->u.l2_l3.eth_type = GetIpfixL2L3HashFieldSelect(V,  etherTypeEn_f     , &l2l3_hash_field);
            field_sel->u.l2_l3.gport = GetIpfixL2L3HashFieldSelect(V,  globalSrcPortEn_f , &l2l3_hash_field);
            field_sel->u.l2_l3.gre_key= GetIpfixL2L3HashFieldSelect(V,  greKeyEn_f        , &l2l3_hash_field);
            field_sel->u.l2_l3.icmp_code= GetIpfixL2L3HashFieldSelect(V,  icmpOpcodeEn_f    , &l2l3_hash_field);
            field_sel->u.l2_l3.icmp_type= GetIpfixL2L3HashFieldSelect(V,  icmpTypeEn_f      , &l2l3_hash_field);
 /*          field_sel->u.l2_l3.merge = GetIpfixL2L3HashFieldSelect(V, isMergeKeyEn_f, &l2l3_hash_field);*/
            field_sel->u.l2_l3.ip_da_mask = GetIpfixL2L3HashFieldSelect(V,  ipDaEn_f, &l2l3_hash_field);
            if (0 != field_sel->u.l2_l3.ip_da_mask)
            {
                field_sel->u.l2_l3.ip_da_mask = field_sel->u.l2_l3.ip_da_mask + 1;
            }
            field_sel->u.l2_l3.ip_da = field_sel->u.l2_l3.ip_da_mask?1:0;
            field_sel->u.l2_l3.ip_sa_mask = GetIpfixL2L3HashFieldSelect(V,  ipSaEn_f, &l2l3_hash_field);
            if (0 != field_sel->u.l2_l3.ip_sa_mask)
            {
                field_sel->u.l2_l3.ip_sa_mask = field_sel->u.l2_l3.ip_sa_mask + 1;
            }
            field_sel->u.l2_l3.ip_sa = field_sel->u.l2_l3.ip_sa_mask?1:0;
            field_sel->u.l2_l3.l4_dst_port = GetIpfixL2L3HashFieldSelect(V,  l4DestPortEn_f, &l2l3_hash_field);
            field_sel->u.l2_l3.l4_src_port= GetIpfixL2L3HashFieldSelect(V,  l4SourcePortEn_f, &l2l3_hash_field);
            field_sel->u.l2_l3.label_num= GetIpfixL2L3HashFieldSelect(V,  labelNumEn_f, &l2l3_hash_field);
            field_sel->u.l2_l3.l4_type= GetIpfixL2L3HashFieldSelect(V,  layer4TypeEn_f, &l2l3_hash_field);
            field_sel->u.l2_l3.logic_port= GetIpfixL2L3HashFieldSelect(V,  logicSrcPortEn_f  , &l2l3_hash_field);
            field_sel->u.l2_l3.mac_da= GetIpfixL2L3HashFieldSelect(V,  macDaEn_f         , &l2l3_hash_field);
            field_sel->u.l2_l3.mac_sa= GetIpfixL2L3HashFieldSelect(V,  macSaEn_f         , &l2l3_hash_field);
            field_sel->u.l2_l3.metadata= GetIpfixL2L3HashFieldSelect(V,  metadataEn_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label0_exp= GetIpfixL2L3HashFieldSelect(V,  mplsExp0En_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label1_exp= GetIpfixL2L3HashFieldSelect(V,  mplsExp1En_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label2_exp= GetIpfixL2L3HashFieldSelect(V,  mplsExp2En_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label0_label= GetIpfixL2L3HashFieldSelect(V, mplsLabel0En_f    , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label1_label= GetIpfixL2L3HashFieldSelect(V, mplsLabel1En_f    , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label2_label= GetIpfixL2L3HashFieldSelect(V, mplsLabel2En_f    , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label0_s= GetIpfixL2L3HashFieldSelect(V, mplsSbit0En_f     , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label1_s= GetIpfixL2L3HashFieldSelect(V, mplsSbit1En_f     , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label2_s= GetIpfixL2L3HashFieldSelect(V, mplsSbit2En_f     , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label0_ttl = GetIpfixL2L3HashFieldSelect(V, mplsTtl0En_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label1_ttl= GetIpfixL2L3HashFieldSelect(V, mplsTtl1En_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.mpls_label2_ttl= GetIpfixL2L3HashFieldSelect(V, mplsTtl2En_f      , &l2l3_hash_field);
            field_sel->u.l2_l3.stag_cfi= GetIpfixL2L3HashFieldSelect(V, stagCfiEn_f       , &l2l3_hash_field);
            field_sel->u.l2_l3.stag_cos= GetIpfixL2L3HashFieldSelect(V, stagCosEn_f       , &l2l3_hash_field);
            field_sel->u.l2_l3.stag_vlan= GetIpfixL2L3HashFieldSelect(V, svlanIdEn_f       , &l2l3_hash_field);
             /*field_sel->u.l2_l3.stag_valid= GetIpfixL2L3HashFieldSelect(V, svlanIdValidEn_f  , &l2l3_hash_field);*/
            field_sel->u.l2_l3.ttl= GetIpfixL2L3HashFieldSelect(V, ttlEn_f           , &l2l3_hash_field);
            field_sel->u.l2_l3.vxlan_vni= GetIpfixL2L3HashFieldSelect(V,  vxlanVniEn_f    , &l2l3_hash_field);
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_set_global_cfg(uint8 lchip, ctc_ipfix_global_cfg_t *ipfix_cfg)
{
    IpfixAgingTimerCtl_m aging_timer;
    IpfixEngineCtl_m    ipfix_ctl;
    IpeAclLookupCtl_m    lookup_ctl;
    uint32 cmd = 0;
    uint64 tmp = 0;
    uint32 core_frequecy = 0;
    uint32 max_age_ms = 0;
    uint32 age_seconds = 0;
    uint32 tick_interval = 0;
    uint32 byte_cnt[2] = {0};
    uint32 pkt_cnt = 0;

    SYS_IPFIX_INIT_CHECK(lchip);

    if (ipfix_cfg->pkt_cnt == 0)
    {
        pkt_cnt = 0;
    }
    else
    {
        pkt_cnt = (ipfix_cfg->pkt_cnt - 1);
    }

    sal_memset(&aging_timer, 0, sizeof(IpfixAgingTimerCtl_m));
    sal_memset(&ipfix_ctl, 0, sizeof(IpfixEngineCtl_m));
    sal_memset(&lookup_ctl, 0, sizeof(IpeAclLookupCtl_m));

    if (ipfix_cfg->times_interval > 0xfffff)
    {
        return CTC_E_INVALID_PARAM;
    }
    /* support 37bits for byte_cnt cfg */
    CTC_MAX_VALUE_CHECK((ipfix_cfg->bytes_cnt >> 32), 0x1F);

    core_frequecy = sys_goldengate_get_core_freq(lchip, 0);
    max_age_ms = (CTC_MAX_UINT32_VALUE / ((core_frequecy * 1000 * 1000 / DOWN_FRE_RATE))) * (p_gg_ipfix_master[lchip]->max_ptr/4);
    if (max_age_ms < SYS_IPFIX_MIN_AGING_INTERVAL || age_seconds > max_age_ms)
    {
        return CTC_E_AGING_INVALID_INTERVAL;
    }

    tmp = (((uint64)core_frequecy * 1000 *1000)/DOWN_FRE_RATE);
    tmp *= ipfix_cfg->aging_interval;
    tick_interval = tmp / (p_gg_ipfix_master[lchip]->max_ptr/4);
    SYS_IPFIX_LOCK(lchip);
    p_gg_ipfix_master[lchip]->aging_interval = ipfix_cfg->aging_interval;
    SYS_IPFIX_UNLOCK(lchip);
    if ((tick_interval == 0) && (ipfix_cfg->aging_interval !=0))
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "aging_interval is 0! \n");
        return CTC_E_AGING_INVALID_INTERVAL;
    }

    cmd = DRV_IOR( IpfixAgingTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aging_timer));
    SetIpfixAgingTimerCtl(V, agingUpdEn_f, &aging_timer, (ipfix_cfg->aging_interval !=0));
    SetIpfixAgingTimerCtl(V, cpuAgingEn_f, &aging_timer, ipfix_cfg->sw_learning_en);

    if (ipfix_cfg->aging_interval)
    {
        SetIpfixAgingTimerCtl(V, interval_f, &aging_timer, tick_interval);
    }

    cmd = DRV_IOW( IpfixAgingTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aging_timer));

    byte_cnt[0] = (ipfix_cfg->bytes_cnt) & 0xffffffff;
    byte_cnt[1] = (ipfix_cfg->bytes_cnt >> 32) & 0x1f;

    cmd = DRV_IOR( IpfixEngineCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_ctl));
    SetIpfixEngineCtl(A, byteCntWraparoundThre_f, &ipfix_ctl, byte_cnt);
    SetIpfixEngineCtl(V, conflictPktExportEn_f, &ipfix_ctl, ipfix_cfg->conflict_export);
    SetIpfixEngineCtl(V, countBasedSamplingMode_f , &ipfix_ctl, ipfix_cfg->sample_mode);
    SetIpfixEngineCtl(V, newFlowExportMode_f  , &ipfix_ctl, ipfix_cfg->new_flow_export_en);
    SetIpfixEngineCtl(V, pktCntWraparoundThre_f , &ipfix_ctl, pkt_cnt);
    SetIpfixEngineCtl(V, ignorTcpClose_f , &ipfix_ctl, ((ipfix_cfg->tcp_end_detect_en)?0:1));
    SetIpfixEngineCtl(V, tsWraparoundThre_f , &ipfix_ctl, ipfix_cfg->times_interval);  /*granularity is 1ms*/
    SetIpfixEngineCtl(V, isCpuSetFlowKeyMode_f , &ipfix_ctl, ipfix_cfg->sw_learning_en);
    SetIpfixEngineCtl(V, unknownPktDestIsVlanId_f, &ipfix_ctl, ipfix_cfg->unkown_pkt_dest_type);
    cmd = DRV_IOW( IpfixEngineCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_ctl));

    return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_get_global_cfg(uint8 lchip, ctc_ipfix_global_cfg_t *ipfix_cfg)
{
    IpfixAgingTimerCtl_m aging_timer;
    IpfixEngineCtl_m    ipfix_ctl;
    uint32 cmd = 0;
    uint32 core_frequecy = 0;
    uint32 max_age_ms = 0;
    uint32 age_seconds = 0;
     /*uint32 tick_interval = 0;*/
    uint32 byte_cnt[2] = {0};

    SYS_IPFIX_INIT_CHECK(lchip);

    sal_memset(&aging_timer, 0, sizeof(IpfixAgingTimerCtl_m));
    sal_memset(&ipfix_ctl, 0, sizeof(IpfixEngineCtl_m));

   core_frequecy = sys_goldengate_get_core_freq(lchip, 0);
    max_age_ms = (CTC_MAX_UINT32_VALUE / (core_frequecy * 1000 * 1000)) * p_gg_ipfix_master[lchip]->max_ptr;
    if (max_age_ms < SYS_IPFIX_MIN_AGING_INTERVAL || age_seconds > max_age_ms)
    {
        return CTC_E_AGING_INVALID_INTERVAL;
    }

    cmd = DRV_IOR( IpfixAgingTimerCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &aging_timer));


    #if 0
    if (GetIpfixAgingTimerCtl(V, agingUpdEn_f, &aging_timer))
    {
        tick_interval = GetIpfixAgingTimerCtl(V, interval_f, &aging_timer);
    }
    else
    {
        tick_interval = 0;
    }

    ipfix_cfg->aging_interval = (tick_interval*p_gg_ipfix_master[lchip]->max_ptr/4)/(core_frequecy * 1000/DOWN_FRE_RATE);
    #endif
    SYS_IPFIX_LOCK(lchip);
    ipfix_cfg->aging_interval = p_gg_ipfix_master[lchip]->aging_interval;
    SYS_IPFIX_UNLOCK(lchip);
    cmd = DRV_IOR( IpfixEngineCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipfix_ctl));
    GetIpfixEngineCtl(A, byteCntWraparoundThre_f, &ipfix_ctl, byte_cnt);

    ipfix_cfg->bytes_cnt = 0;
    ipfix_cfg->bytes_cnt = byte_cnt[1];
    ipfix_cfg->bytes_cnt <<= 32;
    ipfix_cfg->bytes_cnt |= byte_cnt[0];
    ipfix_cfg->sw_learning_en = GetIpfixEngineCtl(V, isCpuSetFlowKeyMode_f , &ipfix_ctl);
    ipfix_cfg->conflict_export = (uint8)GetIpfixEngineCtl(V, conflictPktExportEn_f, &ipfix_ctl);
    ipfix_cfg->sample_mode = (uint8)GetIpfixEngineCtl(V, countBasedSamplingMode_f , &ipfix_ctl);
    ipfix_cfg->pkt_cnt = GetIpfixEngineCtl(V, pktCntWraparoundThre_f , &ipfix_ctl)+1;
    ipfix_cfg->times_interval = GetIpfixEngineCtl(V, tsWraparoundThre_f, &ipfix_ctl);  /*1s*/
    ipfix_cfg->tcp_end_detect_en = !GetIpfixEngineCtl(V, ignorTcpClose_f, &ipfix_ctl);
    ipfix_cfg->new_flow_export_en = GetIpfixEngineCtl(V,newFlowExportMode_f, &ipfix_ctl);
    ipfix_cfg->unkown_pkt_dest_type = GetIpfixEngineCtl(V, unknownPktDestIsVlanId_f, &ipfix_ctl);
    return CTC_E_NONE;
}

extern int32
sys_goldengate_ipfix_register_cb(uint8 lchip, ctc_ipfix_fn_t callback,void *userdata)
{
    SYS_IPFIX_INIT_CHECK(lchip);

    SYS_IPFIX_LOCK(lchip);
    p_gg_ipfix_master[lchip]->ipfix_cb = callback;
    p_gg_ipfix_master[lchip]->user_data = userdata;
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_ipfix_get_cb(uint8 lchip, void **cb, void** user_data)
{
    SYS_IPFIX_INIT_CHECK(lchip);

    SYS_IPFIX_LOCK(lchip);
    *cb = p_gg_ipfix_master[lchip]->ipfix_cb;
    *user_data = p_gg_ipfix_master[lchip]->user_data;
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

int32
sys_goldengate_ipfix_ftm_cb(uint8 lchip, sys_ftm_specs_info_t* specs_info)
{
    CTC_PTR_VALID_CHECK(specs_info);
    SYS_IPFIX_INIT_CHECK(lchip);

    specs_info->used_size = sys_goldengate_ipfix_get_flow_cnt(lchip, 0);

    return CTC_E_NONE;
}
uint32
sys_goldengate_ipfix_get_flow_cnt(uint8 lchip, uint32 detail)
{
    uint32 cmd = 0;
    uint32 cnt = 0;
    uint32 index = 0;
    DsIpfixL2HashKey_m l2_key;
    uint32 entry_num;

    cmd = DRV_IOR( IpfixFlowEntryCounter_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &cnt));

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &entry_num));
    if (detail)
    {
        cmd = DRV_IOR( DsIpfixL2HashKey_t, DRV_ENTRY_FLAG);

        for (index = 0; index < entry_num; index++)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, index*2, cmd, &l2_key));
            if (0 != GetDsIpfixL2HashKey(V, hashKeyType_f , &l2_key))
            {
                SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Entry %d exist! \n", index);
            }
        }
    }

    return cnt;
}

int32
sys_goldengate_ipfix_entry_usage_overflow(uint8 lchip, uint32 intr, void* p_data)
{
    uint32 entry_cnt = 0;

    entry_cnt = sys_goldengate_ipfix_get_flow_cnt(lchip, 0);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ipfix usage overflow!!! \n");
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Current entry cnt:%d \n", entry_cnt);

    return CTC_E_NONE;
}

int32
_sys_goldengate_ipfix_get_str(uint32 flag, char* p_str)
{
    if (flag&CTC_IPFIX_DATA_L2_MCAST_DETECTED)
    {
        sal_strcat((char*)p_str, "L2-Mcast ");
    }
    if (flag&CTC_IPFIX_DATA_L3_MCAST_DETECTED)
    {
        sal_strcat((char*)p_str, "L3-Mcast ");
    }
    if (flag&CTC_IPFIX_DATA_BCAST_DETECTED)
    {
        sal_strcat((char*)p_str, "Bcast ");
    }
    if (flag&CTC_IPFIX_DATA_APS_DETECTED)
    {
        sal_strcat((char*)p_str, "Aps ");
    }
    if (flag&CTC_IPFIX_DATA_ECMP_DETECTED)
    {
        sal_strcat((char*)p_str, "ECMP ");
    }
    if (flag&CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED)
    {
        sal_strcat((char*)p_str, "Frag ");
    }
    if (flag&CTC_IPFIX_DATA_DROP_DETECTED)
    {
        sal_strcat((char*)p_str, "Drop ");
    }
    if (flag&CTC_IPFIX_DATA_LINKAGG_DETECTED)
    {
        sal_strcat((char*)p_str, "Linkagg ");
    }
    if (flag&CTC_IPFIX_DATA_TTL_CHANGE)
    {
        sal_strcat((char*)p_str, "TTL-Change ");
    }
    if (flag&CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED)
    {
        sal_strcat((char*)p_str, "Unkown-Pkt ");
    }
    if (flag&CTC_IPFIX_DATA_CVLAN_TAGGED)
    {
        sal_strcat((char*)p_str, "Cvlan-Valid ");
    }
    if (flag&CTC_IPFIX_DATA_SVLAN_TAGGED)
    {
        sal_strcat((char*)p_str, "Svlan-Valid ");
    }
    return CTC_E_NONE;
}

void
_sys_goldengate_ipfix_process_isr(ctc_ipfix_data_t* info, void* userdata)
{
    char str[256] = {0};
    char* reason_str[9] = {"NO Export","Expired","Tcp Close", "PktCnt Overflow","ByteCnt Overflow", "Ts Overflow", "Conflict", "New Flow", "Dest Change"};
    uint32 tempip = 0;
    char buf[CTC_IPV6_ADDR_STR_LEN];
    uint32 ipv6_address[4] = {0, 0, 0, 0};
    uint8 index = 0;
    uint32 cmd = 0;
    IpfixEngineCtl_m    ipfix_ctl;
    uint8 lchip = (uintptr)userdata;

    if (NULL == info)
    {
        return;
    }
    sal_memset(&ipfix_ctl, 0, sizeof(IpfixEngineCtl_m));
    str[0] = '\0';

    _sys_goldengate_ipfix_get_str(info->flags, str);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT,"\n********Ipfix Export Information******** \n");

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: %s\n", "Dir",(info->dir)?"Egress":"Ingress");
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: %s\n", "Export_reason", reason_str[info->export_reason]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Start_timestamp", (info->start_timestamp));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Last_timestamp", (info->last_timestamp));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%"PRIx64"\n", "Byte_count", (info->byte_count));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Pkt_count", (info->pkt_count));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "First_ttl", (info->ttl));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Min_ttl", (info->min_ttl));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Max_ttl", (info->max_ttl));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x(%s)\n", "Flag", (info->flags), str);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Port_type", (info->port_type));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Logic-port", (info->logic_port));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Gport", (info->gport));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Svlan", (info->svlan));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Svlan_prio", (info->svlan_prio));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Svlan_cfi", (info->svlan_cfi));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Cvlan", (info->cvlan));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Cvlan_prio", (info->cvlan_prio));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Cvlan_cfi", (info->cvlan_cfi));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Ether_type", (info->ether_type));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s:%.4x.%.4x.%.4x \n", "Macsa",
        sal_ntohs(*(unsigned short*)&info->src_mac[0]),  sal_ntohs(*(unsigned short*)&info->src_mac[2]), sal_ntohs(*(unsigned short*)&info->src_mac[4]));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s:%.4x.%.4x.%.4x \n", "Macda",
        sal_ntohs(*(unsigned short*)&info->dst_mac[0]),  sal_ntohs(*(unsigned short*)&info->dst_mac[2]), sal_ntohs(*(unsigned short*)&info->dst_mac[4]));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Dest_gport", (info->dest_gport));

    if (CTC_FLAG_ISSET(info->flags, CTC_IPFIX_DATA_APS_DETECTED))
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Dest_group_id", (info->dest_group_id));
    }
    else
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Dest_group_id", (info->dest_group_id));
    }

    tempip = sal_ntohl(info->l3_info.ipv4.ipda);
    sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: %s\n", "Ipv4_da", buf);

    tempip = sal_ntohl(info->l3_info.ipv4.ipsa);
    sal_inet_ntop(AF_INET, &tempip, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: %s\n", "Ipv4_sa", buf);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Dscp", ((info->key_type == CTC_IPFIX_KEY_HASH_IPV6)?(info->l3_info.ipv6.dscp):(info->l3_info.ipv4.dscp)));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Ecn", (info->l3_info.ipv4.ecn));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Ttl", (info->l3_info.ipv4.ttl));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Vrfid", (info->l3_info.vrfid));

    ipv6_address[0] = sal_ntohl(info->l3_info.ipv6.ipda[0]);
    ipv6_address[1] = sal_ntohl(info->l3_info.ipv6.ipda[1]);
    ipv6_address[2] = sal_ntohl(info->l3_info.ipv6.ipda[2]);
    ipv6_address[3] = sal_ntohl(info->l3_info.ipv6.ipda[3]);

    sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: %s\n", "Ipv6_da", buf);

    ipv6_address[0] = sal_ntohl(info->l3_info.ipv6.ipsa[0]);
    ipv6_address[1] = sal_ntohl(info->l3_info.ipv6.ipsa[1]);
    ipv6_address[2] = sal_ntohl(info->l3_info.ipv6.ipsa[2]);
    ipv6_address[3] = sal_ntohl(info->l3_info.ipv6.ipsa[3]);
    sal_inet_ntop(AF_INET6, ipv6_address, buf, CTC_IPV6_ADDR_STR_LEN);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: %s\n", "Ipv6_sa", buf);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Flow_label", (info->l3_info.ipv6.flow_label));

    if(info->key_type == CTC_IPFIX_KEY_HASH_MPLS || info->key_type == CTC_IPFIX_KEY_HASH_L2_L3)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Label_num", (info->l3_info.mpls.label_num));
        for (index = 0; index < CTC_IPFIX_LABEL_NUM; index++)
        {
            SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  Label%d_exp %-5s: 0x%x\n", index, "", (info->l3_info.mpls.label[index].exp));
            SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  Label%d_ttl %-5s: 0x%x\n", index, "", (info->l3_info.mpls.label[index].ttl));
            SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  Label%d_sbit %-4s: 0x%x\n", index, "", (info->l3_info.mpls.label[index].sbit));
            SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  Label%d_label %-3s: 0x%x\n", index, "", (info->l3_info.mpls.label[index].label));
        }
    }

    /*printf l4 info*/
    if((info->key_type == CTC_IPFIX_KEY_HASH_IPV6) || (info->key_type == CTC_IPFIX_KEY_HASH_L2_L3))
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "L4_type", (info->l4_info.type.l4_type));
    }
    if((info->key_type == CTC_IPFIX_KEY_HASH_IPV4))
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "IP_protocol", (info->l4_info.type.ip_protocol));
    }
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Aware_tunnel_info_en", (info->l4_info.aware_tunnel_info_en));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "L4_sub_type", (info->l4_info.l4_sub_type));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Gre_key", (info->l4_info.gre_key));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Vni", (info->l4_info.vni));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Icmpcode", (info->l4_info.icmp.icmpcode));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "Icmp_type", (info->l4_info.icmp.icmp_type));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "L4_Source_port", (info->l4_info.l4_port.source_port));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_EXPORT, "  %-16s: 0x%x\n", "L4_Dest_port", (info->l4_info.l4_port.dest_port));

    /* delete sw-learning entry form asic, when this entry is aging */
    cmd = DRV_IOR( IpfixEngineCtl_t, DRV_ENTRY_FLAG);
    (DRV_IOCTL(lchip, 0, cmd, &ipfix_ctl));
    if(GetIpfixEngineCtl(V, isCpuSetFlowKeyMode_f , &ipfix_ctl) && 1 == info->export_reason )
    {
        (sys_goldengate_ipfix_delete_entry_by_key(lchip, info));
    }

    return;
}

int32
sys_goldengate_ipfix_show_entry_info(ctc_ipfix_data_t* p_info, void* user_data)
{
    uint16 src_lport = 0;
    uint8 field_sel_id = 0;
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint16 detail = 0;
    uint32 entry_index = 0;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;
    char key_tbl_name[30] = {0};
    char ad_tbl_name[30] = {0};
    char* key_type[5] = {"Mac_key","L2l3_key","Ipv4_key", "Ipv6_key","Mpls_key"};
    char* dir[2] = {"Ingress", "Egress"};
    uint8 lchip = 0;

    if(user_data != NULL)
    {
        entry_index = *(uint32*)user_data;
    }
    src_lport = SYS_MAP_CTC_GPORT_TO_DRV_LPORT(p_info->gport);

    SYS_MAP_GCHIP_TO_LCHIP(CTC_MAP_GPORT_TO_GCHIP(p_info->gport), lchip);

    if (p_info->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_src_port));
        field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_dest_port));
        field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "0x%-9x", entry_index&0x0000ffff);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-11s", key_type[p_info->key_type]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "0x%.4x %-1s", p_info->gport, "");
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-8d", field_sel_id);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d", p_info->max_ttl);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-9d", p_info->min_ttl);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "0x%-9x", p_info->last_timestamp);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-7s\n", dir[p_info->dir]);

    /*show entry detial info*/
    detail = entry_index >> 16;
    if(1 == detail)
    {
    tbl_id = (p_info->key_type==CTC_IPFIX_KEY_HASH_MAC)?DsIpfixL2HashKey_t:
            ((p_info->key_type==CTC_IPFIX_KEY_HASH_IPV4)?DsIpfixL3Ipv4HashKey_t:
            ((p_info->key_type==CTC_IPFIX_KEY_HASH_MPLS)?DsIpfixL3MplsHashKey_t:
            ((p_info->key_type==CTC_IPFIX_KEY_HASH_IPV6)?DsIpfixL3Ipv6HashKey_t:DsIpfixL2L3HashKey_t)));

    drv_goldengate_get_tbl_string_by_id(tbl_id, key_tbl_name);
    drv_goldengate_get_tbl_string_by_id(DsIpfixSessionRecord_t, ad_tbl_name);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"\n");
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"Detail Entry Info\n");
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"-----------------------------------------------\n");
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP," --%-32s :0x%-8x\n", key_tbl_name, entry_index&0x0000ffff);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP," --%-32s :0x%-8x\n", ad_tbl_name, entry_index&0x0000ffff);

    }

    return CTC_E_NONE;
}

int32
sys_goldengate_ipfix_export_stats(uint8 lchip, uint8 exp_reason, uint64 pkt_cnt, uint64 byte_cnt)
{

    SYS_IPFIX_LOCK(lchip);
    switch (exp_reason)
    {
        /* 1. Expired */
        case 1:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[0] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[0] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[0] += byte_cnt;
            break;
        /* 2. Tcp Close */
        case 2:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[1] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[1] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[1] += byte_cnt;
            break;
        /* 3. PktCnt Overflow */
        case 3:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[2] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[2] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[2] += byte_cnt;
            break;
        /* 4. ByteCnt Overflow */
        case 4:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[3] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[3] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[3] += byte_cnt;
            break;
        /* 5.Ts Overflow */
        case 5:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[4] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[4] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[4] += byte_cnt;
            break;
        /* 6. Conflict */
        case 6:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[5] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[5] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[5] += byte_cnt;
            break;
        /* 7. New Flow */
        case 7:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[6] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[6] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[6] += byte_cnt;
            break;
        /* 8. Dest Change*/
        case 8:
            p_gg_ipfix_master[lchip]->exp_cnt_stats[7] ++ ;
            p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[7] += pkt_cnt;
            p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[7] += byte_cnt;
            break;
        default:
            break;
    }
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

#define __SYNC_DMA_INFO__
STATIC int32
_sys_goldengate_ipfix_parser_l2_info(uint8 lchip, DmaIpfixAccL2KeyFifo_m* p_info)
{
    uint32 lport = 0;
    uint32 hash_sel = 0;
    uint32 cmd = 0;
    IpfixL2HashFieldSelect_m  hash_field;
    ctc_ipfix_data_t user_info;
    hw_mac_addr_t             mac_sa   = { 0 };
    hw_mac_addr_t             mac_da   = { 0 };
    uint32 dest_type = 0;
    uint32 byte_cnt[2] = {0};
    uint8 dest_chip_id = 0;
    uint8 gchip_id = 0;
    uint8 use_cvlan = 0;
    DsDestPort_m ds_dest_port;
    uint8 mux_port_type = 0;

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    sal_memset(&user_info, 0, sizeof(ctc_ipfix_data_t));

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    lport = GetDmaIpfixAccL2KeyFifo(V, localPhyPort_f, p_info);
    use_cvlan = GetDmaIpfixAccL2KeyFifo(V, flowL2KeyUseCvlan_f, p_info);
    hash_sel = GetDmaIpfixAccL2KeyFifo(V, flowFieldSel_f, p_info);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lport:%d, hash_sel:%d\n", lport, hash_sel);

    cmd = DRV_IOR(IpfixL2HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    if (GetIpfixL2HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        user_info.gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDmaIpfixAccL2KeyFifo(V, globalSrcPort_f, p_info));
        user_info.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
        gchip_id = SYS_DRV_GPORT_TO_GCHIP(GetDmaIpfixAccL2KeyFifo(V, globalSrcPort_f, p_info));
    }

    if (GetIpfixL2HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL2KeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    if (GetIpfixL2HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL2KeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    if (use_cvlan)
    {
        user_info.cvlan_prio = GetDmaIpfixAccL2KeyFifo(V, stagCos_f, p_info);
        user_info.cvlan = GetDmaIpfixAccL2KeyFifo(V, svlanId_f, p_info);
        user_info.cvlan_cfi = GetDmaIpfixAccL2KeyFifo(V, stagCfi_f, p_info);
        if (GetDmaIpfixAccL2KeyFifo(V, svlanIdValid_f, p_info))
        {
            user_info.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
        }
    }
    else
    {
        user_info.svlan_prio = GetDmaIpfixAccL2KeyFifo(V, stagCos_f, p_info);
        user_info.svlan = GetDmaIpfixAccL2KeyFifo(V, svlanId_f, p_info);
        user_info.svlan_cfi = GetDmaIpfixAccL2KeyFifo(V, stagCfi_f, p_info);
        if (GetDmaIpfixAccL2KeyFifo(V, svlanIdValid_f, p_info))
        {
            user_info.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
        }
    }

    user_info.gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);
    user_info.ether_type = GetDmaIpfixAccL2KeyFifo(V, etherType_f, p_info);

    GetDmaIpfixAccL2KeyFifo(A, macDa_f, p_info, mac_da);
    SYS_GOLDENGATE_SET_USER_MAC(user_info.dst_mac, mac_da);
    GetDmaIpfixAccL2KeyFifo(A, macSa_f, p_info, mac_sa);
    SYS_GOLDENGATE_SET_USER_MAC(user_info.src_mac, mac_sa);

    if (GetDmaIpfixAccL2KeyFifo(V, isEgressKey_f, p_info))
    {
        user_info.dir = CTC_EGRESS;
    }
    else
    {
        user_info.dir = CTC_INGRESS;
    }

    GetDmaIpfixAccL2KeyFifo(A, byteCount_f , p_info, byte_cnt);
    user_info.byte_count = 0;
    user_info.byte_count = byte_cnt[1];
    user_info.byte_count <<= 32;
    user_info.byte_count |= byte_cnt[0];
    dest_type = GetDmaIpfixAccL2KeyFifo(V, destinationType_f, p_info);
    user_info.export_reason = GetDmaIpfixAccL2KeyFifo(V, exportReason_f , p_info);
    user_info.start_timestamp = GetDmaIpfixAccL2KeyFifo(V, firstTs_f , p_info);
    user_info.last_timestamp = GetDmaIpfixAccL2KeyFifo(V, lastTs_f , p_info);
    user_info.max_ttl = GetDmaIpfixAccL2KeyFifo(V, maxTtl_f , p_info);
    user_info.min_ttl = GetDmaIpfixAccL2KeyFifo(V, minTtl_f , p_info);

    if (GetDmaIpfixAccL2KeyFifo(V, fragment_f , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED;
    }
    if (GetDmaIpfixAccL2KeyFifo(V, droppedPacket_f  , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_DROP_DETECTED;
    }
    if (GetDmaIpfixAccL2KeyFifo(V, ttlChanged_f   , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_TTL_CHANGE;
    }

    user_info.pkt_count = GetDmaIpfixAccL2KeyFifo(V, packetCount_f , p_info);

    /* egress ipfix, process muxTag length for pkt byte */
    if(CTC_EGRESS == user_info.dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));
        mux_port_type = GetDsDestPort(V, muxPortType_f, &ds_dest_port);

        /* muxPortType_f: 101/110 is 8bytes; 010 100 is 4bytes */
        if((5 == mux_port_type)||(6 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*8));
        }
        else if((2 == mux_port_type)||(4 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*4));
        }
    }

    user_info.ttl = GetDmaIpfixAccL2KeyFifo(V, ttl_f , p_info);

    switch(dest_type)
    {
        case SYS_IPFIX_UNICAST_DEST:
            dest_chip_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            break;
        case SYS_IPFIX_LINKAGG_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_LINKAGG_DETECTED;
            break;
        case SYS_IPFIX_L2MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L2_MCAST_DETECTED;
            break;
        case SYS_IPFIX_L3MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L3_MCAST_DETECTED;
            break;
        case SYS_IPFIX_BCAST_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gBcast_floodingId_f, p_info);
            user_info.flags |= CTC_IPFIX_DATA_BCAST_DETECTED;
            break;
        case SYS_IPFIX_UNKNOW_PKT_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gUnknownPkt_floodingId_f, p_info);
            user_info.flags |= CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED;
            break;
        case SYS_IPFIX_APS_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gApsGroup_apsGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_APS_DETECTED;
            break;
        case SYS_IPFIX_ECMP_DEST:
            dest_chip_id = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL2KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            user_info.flags |= CTC_IPFIX_DATA_ECMP_DETECTED;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_ipfix_export_stats(lchip, user_info.export_reason, user_info.pkt_count, user_info.byte_count));
    user_info.key_type = CTC_IPFIX_KEY_HASH_MAC;
    SYS_IPFIX_LOCK(lchip);
    if (p_gg_ipfix_master[lchip]->ipfix_cb)
    {
        p_gg_ipfix_master[lchip]->ipfix_cb(&user_info, p_gg_ipfix_master[lchip]->user_data);
    }
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_parser_l2l3_info(uint8 lchip, DmaIpfixAccL2L3KeyFifo_m* p_info)
{
    uint32 lport = 0;
    ctc_ipfix_data_t user_info;
    hw_mac_addr_t             mac_sa   = { 0 };
    hw_mac_addr_t             mac_da   = { 0 };
    uint32 dest_type = 0;
    uint32 byte_cnt[2] = {0};
    uint32 cmd = 0;
    uint32 hash_field_sel = 0;
    IpfixL2L3HashFieldSelect_m  hash_field;
    uint8 dest_chip_id = 0;
    uint8 gchip_id = 0;
    uint32 ether_type = 0;
    ctc_ipfix_hash_field_sel_t field_sel;  /*used for get ip mask*/
    DsDestPort_m ds_dest_port;
    uint8 mux_port_type = 0;

    sal_memset(&user_info, 0, sizeof(ctc_ipfix_data_t));
    sal_memset(&field_sel, 0, sizeof(ctc_ipfix_hash_field_sel_t));

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    lport = GetDmaIpfixAccL2L3KeyFifo(V, localPhyPort_f, p_info);
    ether_type = GetDmaIpfixAccL2L3KeyFifo(V, etherType_f, p_info);
    user_info.gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    hash_field_sel = GetDmaIpfixAccL2L3KeyFifo(V, flowFieldSel_f, p_info);

    cmd = DRV_IOR( IpfixL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_field_sel, cmd, &hash_field));

    if (GetDmaIpfixAccL2L3KeyFifo(V, cvlanIdValid_f , p_info))
    {
        user_info.cvlan = GetDmaIpfixAccL2L3KeyFifo(V, cvlanId_f, p_info);
        user_info.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
    }
    user_info.cvlan_prio = GetDmaIpfixAccL2L3KeyFifo(V, ctagCos_f, p_info);
    user_info.cvlan_cfi = GetDmaIpfixAccL2L3KeyFifo(V, ctagCfi_f, p_info);

    if (GetDmaIpfixAccL2L3KeyFifo(V, svlanIdValid_f , p_info))
    {
        user_info.svlan = GetDmaIpfixAccL2L3KeyFifo(V, svlanId_f, p_info);
        user_info.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
    }
    user_info.svlan_prio = GetDmaIpfixAccL2L3KeyFifo(V, stagCos_f, p_info);
    user_info.svlan_cfi = GetDmaIpfixAccL2L3KeyFifo(V, stagCfi_f, p_info);

    if (GetIpfixL2L3HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        /*convert drv gport to ctc gport*/
        user_info.gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDmaIpfixAccL2L3KeyFifo(V, globalSrcPort_f, p_info));
        user_info.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    if (GetIpfixL2L3HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL2L3KeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    if (GetIpfixL2L3HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL2L3KeyFifo(V, metadata_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    user_info.ether_type = GetDmaIpfixAccL2L3KeyFifo(V, etherType_f, p_info);
    GetDmaIpfixAccL2L3KeyFifo(A, macSa_f, p_info, mac_sa);
    GetDmaIpfixAccL2L3KeyFifo(A, macDa_f, p_info, mac_da);
    SYS_GOLDENGATE_SET_USER_MAC(user_info.src_mac, mac_sa);
    SYS_GOLDENGATE_SET_USER_MAC(user_info.dst_mac, mac_da);

    if (GetDmaIpfixAccL2L3KeyFifo(V, isEgressKey_f, p_info))
    {
        user_info.dir = CTC_EGRESS;
    }
    else
    {
        user_info.dir = CTC_INGRESS;
    }

    GetDmaIpfixAccL2L3KeyFifo(A, byteCount_f , p_info, byte_cnt);
    user_info.byte_count = 0;
    user_info.byte_count = byte_cnt[1];
    user_info.byte_count <<= 32;
    user_info.byte_count |= byte_cnt[0];
    dest_type = GetDmaIpfixAccL2L3KeyFifo(V, destinationType_f , p_info);
    user_info.export_reason = GetDmaIpfixAccL2L3KeyFifo(V, exportReason_f , p_info);
    user_info.start_timestamp = GetDmaIpfixAccL2L3KeyFifo(V, firstTs_f , p_info);
    user_info.last_timestamp = GetDmaIpfixAccL2L3KeyFifo(V, lastTs_f , p_info);
    user_info.max_ttl = GetDmaIpfixAccL2L3KeyFifo(V, maxTtl_f , p_info);
    user_info.min_ttl = GetDmaIpfixAccL2L3KeyFifo(V, minTtl_f , p_info);

    /* process l3 information, using etherType  */
    if (ether_type == 0x0800)
    {
        user_info.l3_info.ipv4.ipsa = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gIpv4_ipSa_f , p_info);
        user_info.l3_info.ipv4.ipda = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gIpv4_ipDa_f , p_info);
        CTC_ERROR_RETURN(sys_goldengate_ipfix_get_hash_field_sel(lchip, hash_field_sel, CTC_IPFIX_KEY_HASH_L2_L3, &field_sel));
        user_info.l3_info.ipv4.ipda_masklen = field_sel.u.l2_l3.ip_da_mask;
        user_info.l3_info.ipv4.ipsa_masklen = field_sel.u.l2_l3.ip_sa_mask;
        user_info.l3_info.ipv4.ecn = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gIpv4_ecn_f , p_info);
        user_info.l3_info.ipv4.dscp = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gIpv4_dscp_f , p_info);
        user_info.l3_info.ipv4.ttl = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gIpv4_ttl_f , p_info);
        user_info.l3_info.vrfid = GetDmaIpfixAccL2L3KeyFifo(V, vrfId12to8_f , p_info);
        user_info.l3_info.vrfid = (user_info.l3_info.vrfid << 8) + GetDmaIpfixAccL2L3KeyFifo(V, uL3_gIpv4_vrfId7to0_f , p_info);
    }
    else if (ether_type == 0x8847)
    {
        user_info.l3_info.mpls.label_num = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_labelNum_f, p_info);
        if (user_info.l3_info.mpls.label_num > CTC_IPFIX_LABEL_NUM)
        {
            return CTC_E_INVALID_PARAM;
        }

        user_info.l3_info.mpls.label[0].exp = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsExp0_f, p_info);
        user_info.l3_info.mpls.label[0].label = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsLabel0_f, p_info);
        user_info.l3_info.mpls.label[0].sbit = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsSbit0_f, p_info);
        user_info.l3_info.mpls.label[0].ttl = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsTtl0_f, p_info);
        user_info.l3_info.mpls.label[1].exp = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsExp1_f, p_info);
        user_info.l3_info.mpls.label[1].label = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsLabel1_f, p_info);
        user_info.l3_info.mpls.label[1].sbit = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsSbit1_f, p_info);
        user_info.l3_info.mpls.label[1].ttl = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsTtl1_f, p_info);
        user_info.l3_info.mpls.label[2].exp = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsExp2_f, p_info);
        user_info.l3_info.mpls.label[2].label = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsLabel2_f, p_info);
        user_info.l3_info.mpls.label[2].sbit = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsSbit2_f, p_info);
        user_info.l3_info.mpls.label[2].ttl = GetDmaIpfixAccL2L3KeyFifo(V, uL3_gMpls_mplsTtl2_f, p_info);
    }

    /* process l4 information */

    user_info.l4_info.aware_tunnel_info_en = GetDmaIpfixAccL2L3KeyFifo(V, isMergeKey_f, p_info);

    user_info.l4_info.type.l4_type = GetDmaIpfixAccL2L3KeyFifo(V, layer4Type_f , p_info);
    /* this check is used for l4_type enum, chip value transfer to api value */
    if (user_info.l4_info.type.l4_type == 13)
    {
        user_info.l4_info.type.l4_type = CTC_IPFIX_L4_TYPE_SCTP;
    }

    user_info.l4_info.l4_sub_type = GetDmaIpfixAccL2L3KeyFifo(V, layer4UserType_f , p_info);

    if (user_info.l4_info.aware_tunnel_info_en)
    {
        if (user_info.l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN)
        {
            if (GetIpfixL2L3HashFieldSelect(V, vxlanVniEn_f, &hash_field))
            {
                user_info.l4_info.vni = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gVxlan_vni_f, p_info);
            }
        }
        else
        {
            if(GetIpfixL2L3HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                user_info.l4_info.gre_key = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gKey_greKey_f, p_info);
            }
        }
    }
    else
    {
        if (user_info.l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_GRE)
        {
            if(GetIpfixL2L3HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                user_info.l4_info.gre_key = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gKey_greKey_f, p_info);
            }
        }
        else if (user_info.l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_ICMP)
        {
           if (GetIpfixL2L3HashFieldSelect(V, icmpOpcodeEn_f, &hash_field))
           {
               user_info.l4_info.icmp.icmpcode = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info)&0xff;
           }
           if (GetIpfixL2L3HashFieldSelect(V, icmpTypeEn_f, &hash_field))
           {
               user_info.l4_info.icmp.icmp_type = (GetDmaIpfixAccL2L3KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info)>>8)&0xff;
           }
        }
        else if ((user_info.l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_UDP)&&(user_info.l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN))
        {
            if (GetIpfixL2L3HashFieldSelect(V, vxlanVniEn_f, &hash_field))
            {
                user_info.l4_info.vni = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gVxlan_vni_f, p_info);
            }
        }
        else
        {
            if (GetIpfixL2L3HashFieldSelect(V, l4DestPortEn_f, &hash_field))
            {
                user_info.l4_info.l4_port.dest_port = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gPort_l4DestPort_f, p_info);
            }
            if (GetIpfixL2L3HashFieldSelect(V, l4SourcePortEn_f, &hash_field))
            {
                user_info.l4_info.l4_port.source_port = GetDmaIpfixAccL2L3KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info);
            }
        }
    }

    /* parser ad info*/
    if (GetDmaIpfixAccL2L3KeyFifo(V, fragment_f , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED;
    }
    if (GetDmaIpfixAccL2L3KeyFifo(V, droppedPacket_f  , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_DROP_DETECTED;
    }
    if (GetDmaIpfixAccL2L3KeyFifo(V, ttlChanged_f   , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_TTL_CHANGE;
    }

    user_info.pkt_count = GetDmaIpfixAccL2L3KeyFifo(V, packetCount_f , p_info);
    user_info.ttl = GetDmaIpfixAccL2L3KeyFifo(V, ttl_f , p_info);

    /* egress ipfix, process muxTag length for pkt byte */
    if(CTC_EGRESS == user_info.dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));
        mux_port_type = GetDsDestPort(V, muxPortType_f, &ds_dest_port);

        /* muxPortType_f: 101/110 is 8bytes; 010 100 is 4bytes */
        if((5 == mux_port_type)||(6 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*8));
        }
        else if((2 == mux_port_type)||(4 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*4));
        }
    }

    switch(dest_type)
    {
        case SYS_IPFIX_UNICAST_DEST:
            dest_chip_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            break;
        case SYS_IPFIX_LINKAGG_DEST:
            user_info.dest_gport = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_LINKAGG_DETECTED;
            break;
        case SYS_IPFIX_L2MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L2_MCAST_DETECTED;
            break;
        case SYS_IPFIX_L3MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L3_MCAST_DETECTED;
            break;
        case SYS_IPFIX_BCAST_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gBcast_floodingId_f, p_info);
            user_info.flags |= CTC_IPFIX_DATA_BCAST_DETECTED;
            break;
        case SYS_IPFIX_UNKNOW_PKT_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gUnknownPkt_floodingId_f, p_info);
            user_info.flags |= CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED;
            break;
        case SYS_IPFIX_APS_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gApsGroup_apsGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_APS_DETECTED;
            break;
        case SYS_IPFIX_ECMP_DEST:
            dest_chip_id = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL2L3KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            user_info.flags |= CTC_IPFIX_DATA_ECMP_DETECTED;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    user_info.key_type = CTC_IPFIX_KEY_HASH_L2_L3;
    CTC_ERROR_RETURN(sys_goldengate_ipfix_export_stats(lchip, user_info.export_reason, user_info.pkt_count, user_info.byte_count));
    SYS_IPFIX_LOCK(lchip);
    if (p_gg_ipfix_master[lchip]->ipfix_cb)
    {
        p_gg_ipfix_master[lchip]->ipfix_cb(&user_info, p_gg_ipfix_master[lchip]->user_data);
    }
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_parser_l3_ipv4_info(uint8 lchip, DmaIpfixAccL3Ipv4KeyFifo_m* p_info)
{
    uint32 lport = 0;
    ctc_ipfix_data_t user_info;
    uint32 dest_type = 0;
    uint32 byte_cnt[2] = {0};
    uint32 hash_field_sel = 0;
    IpfixL3Ipv4HashFieldSelect_m  hash_field;
    uint32 cmd = 0;
    uint8 dest_chip_id = 0;
    uint8 gchip_id = 0;
    ctc_ipfix_hash_field_sel_t field_sel;  /*used for get ip mask*/
    DsDestPort_m ds_dest_port;
    uint8 mux_port_type = 0;

    sal_memset(&user_info, 0, sizeof(ctc_ipfix_data_t));
    sal_memset(&field_sel, 0, sizeof(ctc_ipfix_hash_field_sel_t));

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    lport = GetDmaIpfixAccL3Ipv4KeyFifo(V, localPhyPort_f, p_info);
    user_info.gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);
    hash_field_sel = GetDmaIpfixAccL3Ipv4KeyFifo(V, flowFieldSel_f, p_info);

    cmd = DRV_IOR( IpfixL3Ipv4HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_field_sel, cmd, &hash_field));

    if (GetDmaIpfixAccL3Ipv4KeyFifo(V, isEgressKey_f, p_info))
    {
        user_info.dir = CTC_EGRESS;
    }
    else
    {
        user_info.dir = CTC_INGRESS;
    }

    if (GetIpfixL3Ipv4HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        user_info.gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDmaIpfixAccL3Ipv4KeyFifo(V, globalSrcPort_f, p_info));
        user_info.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    if (GetIpfixL3Ipv4HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL3Ipv4KeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    if (GetIpfixL3Ipv4HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL3Ipv4KeyFifo(V, metadata_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    GetDmaIpfixAccL3Ipv4KeyFifo(A, byteCount_f , p_info, byte_cnt);
    user_info.byte_count = 0;
    user_info.byte_count = byte_cnt[1];
    user_info.byte_count <<= 32;
    user_info.byte_count |= byte_cnt[0];
    dest_type = GetDmaIpfixAccL3Ipv4KeyFifo(V, destinationType_f , p_info);
    user_info.export_reason = GetDmaIpfixAccL3Ipv4KeyFifo(V, exportReason_f , p_info);
    user_info.start_timestamp = GetDmaIpfixAccL3Ipv4KeyFifo(V, firstTs_f , p_info);
    user_info.last_timestamp = GetDmaIpfixAccL3Ipv4KeyFifo(V, lastTs_f , p_info);
    user_info.max_ttl = GetDmaIpfixAccL3Ipv4KeyFifo(V, maxTtl_f , p_info);
    user_info.min_ttl = GetDmaIpfixAccL3Ipv4KeyFifo(V, minTtl_f , p_info);
    user_info.l3_info.ipv4.dscp = GetDmaIpfixAccL3Ipv4KeyFifo(V, dscp_f , p_info);

    /*ipv4 key*/

    user_info.l4_info.aware_tunnel_info_en = GetDmaIpfixAccL3Ipv4KeyFifo(V, isMergeKey_f, p_info);
    user_info.l3_info.ipv4.ipsa = GetDmaIpfixAccL3Ipv4KeyFifo(V, ipSa_f , p_info);
    user_info.l3_info.ipv4.ipda = GetDmaIpfixAccL3Ipv4KeyFifo(V, ipDa_f , p_info);
    CTC_ERROR_RETURN(sys_goldengate_ipfix_get_hash_field_sel(lchip, hash_field_sel, CTC_IPFIX_KEY_HASH_IPV4, &field_sel));
    user_info.l3_info.ipv4.ipda_masklen = field_sel.u.ipv4.ip_da_mask;
    user_info.l3_info.ipv4.ipsa_masklen = field_sel.u.ipv4.ip_sa_mask;

    /* process l4 information */
    user_info.l4_info.type.ip_protocol = GetDmaIpfixAccL3Ipv4KeyFifo(V, layer3HeaderProtocol_f, p_info);
    user_info.l4_info.l4_sub_type = GetDmaIpfixAccL3Ipv4KeyFifo(V, layer4UserType_f, p_info);

    if (user_info.l4_info.aware_tunnel_info_en)
    {
        if (user_info.l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN)
        {
            if (GetIpfixL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &hash_field))
            {
                user_info.l4_info.vni = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gVxlan_vni_f, p_info);
            }
        }
        else
        {
            if(GetIpfixL3Ipv4HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                user_info.l4_info.gre_key = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gKey_greKey_f, p_info);
            }
        }
    }
    else
    {
        if (user_info.l4_info.type.ip_protocol == SYS_IPFIX_GRE_PROTOCOL)
        {
            if(GetIpfixL3Ipv4HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                user_info.l4_info.gre_key = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gKey_greKey_f, p_info);
            }
        }
        else if (user_info.l4_info.type.ip_protocol == SYS_IPFIX_ICMP_PROTOCOL)
        {
           if (GetIpfixL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f, &hash_field))
           {
               user_info.l4_info.icmp.icmpcode = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info)&0xff;
           }
           if (GetIpfixL3Ipv4HashFieldSelect(V, icmpTypeEn_f, &hash_field))
           {
               user_info.l4_info.icmp.icmp_type = (GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info)>>8)&0xff;
           }
        }
        else if ((user_info.l4_info.type.ip_protocol == SYS_IPFIX_UDP_PROTOCOL) && (user_info.l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN))
        {
            if (GetIpfixL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &hash_field))
            {
                user_info.l4_info.vni = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gVxlan_vni_f, p_info);
            }
        }
        else
        {
            if (GetIpfixL3Ipv4HashFieldSelect(V, l4DestPortEn_f, &hash_field))
            {
                user_info.l4_info.l4_port.dest_port = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gPort_l4DestPort_f, p_info);
            }
            if (GetIpfixL3Ipv4HashFieldSelect(V, l4SourcePortEn_f, &hash_field))
            {
                user_info.l4_info.l4_port.source_port = GetDmaIpfixAccL3Ipv4KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info);
            }
        }
    }

    /* parser ad info*/
    if (GetDmaIpfixAccL3Ipv4KeyFifo(V, fragment_f , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED;
    }
    if (GetDmaIpfixAccL3Ipv4KeyFifo(V, droppedPacket_f  , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_DROP_DETECTED;
    }
    if (GetDmaIpfixAccL3Ipv4KeyFifo(V, ttlChanged_f   , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_TTL_CHANGE;
    }

    user_info.pkt_count = GetDmaIpfixAccL3Ipv4KeyFifo(V, packetCount_f , p_info);
    user_info.ttl = GetDmaIpfixAccL3Ipv4KeyFifo(V, ttl_f , p_info);

    /* egress ipfix, process muxTag length for pkt byte */
    if(CTC_EGRESS == user_info.dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));
        mux_port_type = GetDsDestPort(V, muxPortType_f, &ds_dest_port);

        /* muxPortType_f: 101/110 is 8bytes; 010 100 is 4bytes */
        if((5 == mux_port_type)||(6 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*8));
        }
        else if((2 == mux_port_type)||(4 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*4));
        }
    }

    switch(dest_type)
    {
        case SYS_IPFIX_UNICAST_DEST:
            dest_chip_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            break;
        case SYS_IPFIX_LINKAGG_DEST:
            user_info.dest_gport = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_LINKAGG_DETECTED;
            break;
        case SYS_IPFIX_L2MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L2_MCAST_DETECTED;
            break;
        case SYS_IPFIX_L3MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L3_MCAST_DETECTED;
            break;
        case SYS_IPFIX_BCAST_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gBcast_floodingId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_BCAST_DETECTED;
            break;
        case SYS_IPFIX_UNKNOW_PKT_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gUnknownPkt_floodingId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED;
            break;
        case SYS_IPFIX_APS_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gApsGroup_apsGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_APS_DETECTED;
            break;
        case SYS_IPFIX_ECMP_DEST:
            dest_chip_id = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL3Ipv4KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            user_info.flags |= CTC_IPFIX_DATA_ECMP_DETECTED;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    user_info.key_type = CTC_IPFIX_KEY_HASH_IPV4;
    CTC_ERROR_RETURN(sys_goldengate_ipfix_export_stats(lchip, user_info.export_reason, user_info.pkt_count, user_info.byte_count));
    SYS_IPFIX_LOCK(lchip);
    if (p_gg_ipfix_master[lchip]->ipfix_cb)
    {
        p_gg_ipfix_master[lchip]->ipfix_cb(&user_info, p_gg_ipfix_master[lchip]->user_data);
    }
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_parser_l3_ipv6_info(uint8 lchip, DmaIpfixAccL3Ipv6KeyFifo_m* p_info)
{
    uint32 lport = 0;
    ctc_ipfix_data_t user_info;
    uint32 dest_type = 0;
    uint32 byte_cnt[2] = {0};
    uint32 hash_sel = 0;
    uint32 cmd = 0;
    IpfixL3Ipv6HashFieldSelect_m  hash_field;
    uint8 dest_chip_id = 0;
    uint8 gchip_id = 0;
    ctc_ipfix_hash_field_sel_t field_sel;  /*used for get ip mask*/
    uint32 ipv6_address[4] = {0, 0, 0, 0};
    DsDestPort_m ds_dest_port;
    uint8 mux_port_type = 0;

    sal_memset(&user_info, 0, sizeof(ctc_ipfix_data_t));
    sal_memset(&field_sel, 0, sizeof(ctc_ipfix_hash_field_sel_t));

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    hash_sel = GetDmaIpfixAccL3Ipv6KeyFifo(V, flowFieldSel_f, p_info);

    cmd = DRV_IOR(IpfixL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    lport = GetDmaIpfixAccL3Ipv6KeyFifo(V, localPhyPort_f, p_info);
    user_info.gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    if (GetIpfixL3Ipv6HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        user_info.gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDmaIpfixAccL3Ipv6KeyFifo(V, globalSrcPort_f, p_info));
        user_info.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    if (GetIpfixL3Ipv6HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL3Ipv6KeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    if (GetIpfixL3Ipv6HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL3Ipv6KeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    if (GetDmaIpfixAccL3Ipv6KeyFifo(V, isEgressKey_f, p_info))
    {
        user_info.dir = CTC_EGRESS;
    }
    else
    {
        user_info.dir = CTC_INGRESS;
    }

    GetDmaIpfixAccL3Ipv6KeyFifo(A, byteCount_f , p_info, byte_cnt);
    user_info.byte_count = 0;
    user_info.byte_count = byte_cnt[1];
    user_info.byte_count <<= 32;
    user_info.byte_count |= byte_cnt[0];
    dest_type = GetDmaIpfixAccL3Ipv6KeyFifo(V, destinationType_f , p_info);
    user_info.export_reason = GetDmaIpfixAccL3Ipv6KeyFifo(V, exportReason_f , p_info);
    user_info.start_timestamp = GetDmaIpfixAccL3Ipv6KeyFifo(V, firstTs_f , p_info);
    user_info.last_timestamp = GetDmaIpfixAccL3Ipv6KeyFifo(V, lastTs_f , p_info);
    user_info.max_ttl = GetDmaIpfixAccL3Ipv6KeyFifo(V, maxTtl_f , p_info);
    user_info.min_ttl = GetDmaIpfixAccL3Ipv6KeyFifo(V, minTtl_f , p_info);

    GetDmaIpfixAccL3Ipv6KeyFifo(A, ipSa_f , p_info,  ipv6_address);
    user_info.l3_info.ipv6.ipsa[0] = ipv6_address[3];
    user_info.l3_info.ipv6.ipsa[1] = ipv6_address[2];
    user_info.l3_info.ipv6.ipsa[2] = ipv6_address[1];
    user_info.l3_info.ipv6.ipsa[3] = ipv6_address[0];


    GetDmaIpfixAccL3Ipv6KeyFifo(A, ipDa_f , p_info, ipv6_address);
    user_info.l3_info.ipv6.ipda[0] = ipv6_address[3];
    user_info.l3_info.ipv6.ipda[1] = ipv6_address[2];
    user_info.l3_info.ipv6.ipda[2] = ipv6_address[1];
    user_info.l3_info.ipv6.ipda[3] = ipv6_address[0];

    CTC_ERROR_RETURN(sys_goldengate_ipfix_get_hash_field_sel(lchip, hash_sel, CTC_IPFIX_KEY_HASH_IPV6, &field_sel));
    user_info.l3_info.ipv6.ipda_masklen = field_sel.u.ipv6.ip_da_mask;
    user_info.l3_info.ipv6.ipsa_masklen = field_sel.u.ipv6.ip_sa_mask;
    /* parser l4 info */

    user_info.l4_info.aware_tunnel_info_en = GetDmaIpfixAccL3Ipv6KeyFifo(V, isMergeKey_f, p_info);

    user_info.l4_info.type.l4_type = GetDmaIpfixAccL3Ipv6KeyFifo(V, layer4Type_f , p_info);
    /* this check is used for l4_type enum, chip value transfer to api value */
    if (user_info.l4_info.type.l4_type == 13)
    {
        user_info.l4_info.type.l4_type = CTC_IPFIX_L4_TYPE_SCTP;
    }

    if(GetDmaIpfixAccL3Ipv6KeyFifo(V, isVxlan_f , p_info))
    {
        user_info.l4_info.l4_sub_type = CTC_IPFIX_L4_SUB_TYPE_VXLAN;
    }
    /* parser l4 info, careful ipv6.flow_label is l3 info */
    if (GetIpfixL3Ipv6HashFieldSelect(V, ipv6FlowLabelEn_f, &hash_field))
    {
        user_info.l3_info.ipv6.flow_label = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gKey_greKey_f, p_info);
    }
    else
    {
        if (user_info.l4_info.aware_tunnel_info_en)
        {
            if (GetIpfixL3Ipv6HashFieldSelect(V, isVxlanEn_f, &hash_field))
            {
                 if (GetIpfixL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &hash_field))
                 {
                     user_info.l4_info.vni = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gVxlan_vni_f, p_info);
                 }
            }
            else
            {
                if(GetIpfixL3Ipv6HashFieldSelect(V, greKeyEn_f, &hash_field))
                {
                    user_info.l4_info.gre_key = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gKey_greKey_f, p_info);
                }
            }
        }
        else
        {
            if (user_info.l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_GRE)
            {
                if(GetIpfixL3Ipv6HashFieldSelect(V, greKeyEn_f, &hash_field))
                {
                    user_info.l4_info.gre_key = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gKey_greKey_f, p_info);
                }
            }
            else if (user_info.l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_ICMP)
            {
               if (GetIpfixL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f, &hash_field))
               {
                   user_info.l4_info.icmp.icmpcode = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info)&0xff;
               }
               if (GetIpfixL3Ipv6HashFieldSelect(V, icmpTypeEn_f, &hash_field))
               {
                   user_info.l4_info.icmp.icmp_type = (GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info)>>8)&0xff;
               }
            }

            else if ((user_info.l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_UDP) && GetIpfixL3Ipv6HashFieldSelect(V, isVxlanEn_f, &hash_field))
            {
                 if (GetIpfixL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &hash_field))
                 {
                     user_info.l4_info.vni = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gVxlan_vni_f, p_info);
                 }
            }
            else
            {
                if (GetIpfixL3Ipv6HashFieldSelect(V, l4DestPortEn_f, &hash_field))
                {
                    user_info.l4_info.l4_port.dest_port = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gPort_l4DestPort_f, p_info);
                }
                if (GetIpfixL3Ipv6HashFieldSelect(V, l4SourcePortEn_f, &hash_field))
                {
                    user_info.l4_info.l4_port.source_port = GetDmaIpfixAccL3Ipv6KeyFifo(V, uL4_gPort_l4SourcePort_f, p_info);
                }
            }
        }
    }

    /* parser ad info*/
    if (GetDmaIpfixAccL3Ipv6KeyFifo(V, fragment_f , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED;
    }
    if (GetDmaIpfixAccL3Ipv6KeyFifo(V, droppedPacket_f  , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_DROP_DETECTED;
    }
    if (GetDmaIpfixAccL3Ipv6KeyFifo(V, ttlChanged_f   , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_TTL_CHANGE;
    }

    user_info.pkt_count = GetDmaIpfixAccL3Ipv6KeyFifo(V, packetCount_f , p_info);
    user_info.ttl = GetDmaIpfixAccL3Ipv6KeyFifo(V, ttl_f , p_info);
    user_info.l3_info.ipv6.dscp= GetDmaIpfixAccL3Ipv6KeyFifo(V, dscp_f, p_info);

    /* egress ipfix, process muxTag length for pkt byte */
    if(CTC_EGRESS == user_info.dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));
        mux_port_type = GetDsDestPort(V, muxPortType_f, &ds_dest_port);

        /* muxPortType_f: 101/110 is 8bytes; 010 100 is 4bytes */
        if((5 == mux_port_type)||(6 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*8));
        }
        else if((2 == mux_port_type)||(4 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*4));
        }
    }

    switch(dest_type)
    {
        case SYS_IPFIX_UNICAST_DEST:
            dest_chip_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            break;
        case SYS_IPFIX_LINKAGG_DEST:
            user_info.dest_gport = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_LINKAGG_DETECTED;
            break;
        case SYS_IPFIX_L2MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L2_MCAST_DETECTED;
            break;
        case SYS_IPFIX_L3MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L3_MCAST_DETECTED;
            break;
        case SYS_IPFIX_BCAST_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gBcast_floodingId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_BCAST_DETECTED;
            break;
        case SYS_IPFIX_UNKNOW_PKT_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gUnknownPkt_floodingId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED;
            break;
        case SYS_IPFIX_APS_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gApsGroup_apsGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_APS_DETECTED;
            break;
        case SYS_IPFIX_ECMP_DEST:
            dest_chip_id = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL3Ipv6KeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            user_info.flags |= CTC_IPFIX_DATA_ECMP_DETECTED;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    user_info.key_type = CTC_IPFIX_KEY_HASH_IPV6;
    CTC_ERROR_RETURN(sys_goldengate_ipfix_export_stats(lchip, user_info.export_reason, user_info.pkt_count, user_info.byte_count));
    SYS_IPFIX_LOCK(lchip);
    if (p_gg_ipfix_master[lchip]->ipfix_cb)
    {
        p_gg_ipfix_master[lchip]->ipfix_cb(&user_info, p_gg_ipfix_master[lchip]->user_data);
    }
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_parser_l3_mpls_info(uint8 lchip, DmaIpfixAccL3MplsKeyFifo_m* p_info)
{
    uint32 lport = 0;
    ctc_ipfix_data_t user_info;
    uint32 dest_type = 0;
    uint32 byte_cnt[2] = {0};
    uint32 cmd = 0;
    IpfixL3MplsHashFieldSelect_m  hash_field;
    uint32 hash_field_sel = 0;
    uint8 dest_chip_id = 0;
    uint8 gchip_id = 0;
    DsDestPort_m ds_dest_port;
    uint8 mux_port_type = 0;

    sal_memset(&user_info, 0, sizeof(ctc_ipfix_data_t));

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    lport = GetDmaIpfixAccL3MplsKeyFifo(V, localPhyPort_f, p_info);
    user_info.gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    hash_field_sel = GetDmaIpfixAccL3MplsKeyFifo(V, flowFieldSel_f, p_info);

    cmd = DRV_IOR(IpfixL3MplsHashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_field_sel, cmd, &hash_field));

    if (GetIpfixL3MplsHashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        user_info.gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDmaIpfixAccL3MplsKeyFifo(V, globalSrcPort_f, p_info));
        user_info.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    if (GetIpfixL3MplsHashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL3MplsKeyFifo(V, globalSrcPort_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    if (GetIpfixL3MplsHashFieldSelect(V, metadataEn_f, &hash_field))
    {
        user_info.logic_port = GetDmaIpfixAccL3MplsKeyFifo(V, metadata_f, p_info);
        user_info.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    if (GetDmaIpfixAccL3MplsKeyFifo(V, isEgressKey_f, p_info))
    {
        user_info.dir = CTC_EGRESS;
    }
    else
    {
        user_info.dir = CTC_INGRESS;
    }

    GetDmaIpfixAccL3MplsKeyFifo(A, byteCount_f , p_info, byte_cnt);
    user_info.byte_count = 0;
    user_info.byte_count = byte_cnt[1];
    user_info.byte_count <<= 32;
    user_info.byte_count |= byte_cnt[0];
    dest_type = GetDmaIpfixAccL3MplsKeyFifo(V, destinationType_f , p_info);
    user_info.export_reason = GetDmaIpfixAccL3MplsKeyFifo(V, exportReason_f , p_info);
    user_info.start_timestamp = GetDmaIpfixAccL3MplsKeyFifo(V, firstTs_f , p_info);
    user_info.last_timestamp = GetDmaIpfixAccL3MplsKeyFifo(V, lastTs_f , p_info);
    user_info.max_ttl = GetDmaIpfixAccL3MplsKeyFifo(V, maxTtl_f , p_info);
    user_info.min_ttl = GetDmaIpfixAccL3MplsKeyFifo(V, minTtl_f , p_info);

    user_info.l3_info.mpls.label_num = GetDmaIpfixAccL3MplsKeyFifo(V, labelNum_f, p_info);
    if (user_info.l3_info.mpls.label_num > CTC_IPFIX_LABEL_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    user_info.l3_info.mpls.label[0].exp = GetDmaIpfixAccL3MplsKeyFifo(V, mplsExp0_f, p_info);
    user_info.l3_info.mpls.label[0].label = GetDmaIpfixAccL3MplsKeyFifo(V, mplsLabel0_f, p_info);
    user_info.l3_info.mpls.label[0].sbit = GetDmaIpfixAccL3MplsKeyFifo(V, mplsSbit0_f, p_info);
    user_info.l3_info.mpls.label[0].ttl = GetDmaIpfixAccL3MplsKeyFifo(V, mplsTtl0_f, p_info);
    user_info.l3_info.mpls.label[1].exp = GetDmaIpfixAccL3MplsKeyFifo(V, mplsExp1_f, p_info);
    user_info.l3_info.mpls.label[1].label = GetDmaIpfixAccL3MplsKeyFifo(V, mplsLabel1_f, p_info);
    user_info.l3_info.mpls.label[1].sbit = GetDmaIpfixAccL3MplsKeyFifo(V, mplsSbit1_f, p_info);
    user_info.l3_info.mpls.label[1].ttl = GetDmaIpfixAccL3MplsKeyFifo(V, mplsTtl1_f, p_info);
    user_info.l3_info.mpls.label[2].exp = GetDmaIpfixAccL3MplsKeyFifo(V, mplsExp2_f, p_info);
    user_info.l3_info.mpls.label[2].label = GetDmaIpfixAccL3MplsKeyFifo(V, mplsLabel2_f, p_info);
    user_info.l3_info.mpls.label[2].sbit = GetDmaIpfixAccL3MplsKeyFifo(V, mplsSbit2_f, p_info);
    user_info.l3_info.mpls.label[2].ttl = GetDmaIpfixAccL3MplsKeyFifo(V, mplsTtl2_f, p_info);

    if (GetDmaIpfixAccL3MplsKeyFifo(V, fragment_f , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED;
    }
    if (GetDmaIpfixAccL3MplsKeyFifo(V, droppedPacket_f  , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_DROP_DETECTED;
    }
    if (GetDmaIpfixAccL3MplsKeyFifo(V, ttlChanged_f   , p_info))
    {
        user_info.flags |= CTC_IPFIX_DATA_TTL_CHANGE;
    }

    user_info.pkt_count = GetDmaIpfixAccL3MplsKeyFifo(V, packetCount_f , p_info);
    user_info.ttl = GetDmaIpfixAccL3MplsKeyFifo(V, ttl_f , p_info);
    /* egress ipfix, process muxTag length for pkt byte */
    if(CTC_EGRESS == user_info.dir)
    {
        cmd = DRV_IOR(DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &ds_dest_port));
        mux_port_type = GetDsDestPort(V, muxPortType_f, &ds_dest_port);

        /* muxPortType_f: 101/110 is 8bytes; 010 100 is 4bytes */
        if((5 == mux_port_type)||(6 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*8));
        }
        else if((2 == mux_port_type)||(4 == mux_port_type))
        {
            user_info.byte_count = (user_info.byte_count - (user_info.pkt_count*4));
        }
    }

    switch(dest_type)
    {
        case SYS_IPFIX_UNICAST_DEST:
            dest_chip_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            break;
        case SYS_IPFIX_LINKAGG_DEST:
            user_info.dest_gport = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_LINKAGG_DETECTED;
            break;
        case SYS_IPFIX_L2MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L2_MCAST_DETECTED;
            break;
        case SYS_IPFIX_L3MC_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_L3_MCAST_DETECTED;
            break;
        case SYS_IPFIX_BCAST_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gBcast_floodingId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_BCAST_DETECTED;
            break;
        case SYS_IPFIX_UNKNOW_PKT_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gUnknownPkt_floodingId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED;
            break;
        case SYS_IPFIX_APS_DEST:
            user_info.dest_group_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gApsGroup_apsGroupId_f , p_info);
            user_info.flags |= CTC_IPFIX_DATA_APS_DETECTED;
            break;
        case SYS_IPFIX_ECMP_DEST:
            dest_chip_id = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_info);
            user_info.dest_gport = GetDmaIpfixAccL3MplsKeyFifo(V, uDestinationInfo_gUcastToNormal_destId_f , p_info);
            user_info.dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip_id, user_info.dest_gport);
            user_info.flags |= CTC_IPFIX_DATA_ECMP_DETECTED;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    user_info.key_type = CTC_IPFIX_KEY_HASH_MPLS;
    CTC_ERROR_RETURN(sys_goldengate_ipfix_export_stats(lchip, user_info.export_reason, user_info.pkt_count, user_info.byte_count));
    SYS_IPFIX_LOCK(lchip);
    if (p_gg_ipfix_master[lchip]->ipfix_cb)
    {
        p_gg_ipfix_master[lchip]->ipfix_cb(&user_info, p_gg_ipfix_master[lchip]->user_data);
    }
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
   @brief sync dma ipfix info
 */
int32
sys_goldengate_ipfix_sync_data(uint8 lchip, void* p_dma_info)
{
    uint16 entry_num = 0;
    uint16 index = 0;
    DmaIpfixAccL2KeyFifo_m* p_ipfix_l2 = NULL;
    DmaIpfixAccL2L3KeyFifo_m* p_ipfix_l2l3 = NULL;
    DmaIpfixAccL3Ipv4KeyFifo_m* p_ipfix_ipv4 = NULL;
    DmaIpfixAccL3Ipv6KeyFifo_m* p_ipfix_ipv6 = NULL;
    DmaIpfixAccL3MplsKeyFifo_m* p_ipfix_mpls = NULL;
    sys_dma_info_t* p_info = (sys_dma_info_t*)p_dma_info;

    DmaIpfixAccFifo_m* p_temp_fifo = NULL;
    uint32 key_type = 0;

    entry_num = p_info->entry_num;

    for (index = 0; index < entry_num; index++)
    {
        p_temp_fifo = ((DmaIpfixAccFifo_m*)p_info->p_data+index);
        p_ipfix_l2 = (DmaIpfixAccL2KeyFifo_m*)p_temp_fifo;

        key_type = GetDmaIpfixAccL2KeyFifo(V, hashKeyType_f, p_ipfix_l2);

        switch(key_type)
        {
            case SYS_IPFIX_HASH_TYPE_L2:
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_parser_l2_info(lchip, p_ipfix_l2));
                break;

            case SYS_IPFIX_HASH_TYPE_L2L3:
                p_ipfix_l2l3 = (DmaIpfixAccL2L3KeyFifo_m*)p_temp_fifo;
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_parser_l2l3_info(lchip, p_ipfix_l2l3));
                break;

            case SYS_IPFIX_HASH_TYPE_IPV4:
                p_ipfix_ipv4 = (DmaIpfixAccL3Ipv4KeyFifo_m*)p_temp_fifo;
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_parser_l3_ipv4_info(lchip, p_ipfix_ipv4));
                break;

            case SYS_IPFIX_HASH_TYPE_IPV6:
                p_ipfix_ipv6 = (DmaIpfixAccL3Ipv6KeyFifo_m*)p_temp_fifo;
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_parser_l3_ipv6_info(lchip, p_ipfix_ipv6));
                break;

            case SYS_IPFIX_HASH_TYPE_MPLS:
                p_ipfix_mpls = (DmaIpfixAccL3MplsKeyFifo_m*)p_temp_fifo;
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_parser_l3_mpls_info(lchip, p_ipfix_mpls));
                break;

            default:
                SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Invalid IPfix Record!! \n");
                break;

        }
    }

    return CTC_E_NONE;
}

#define __CPU_INTERFACE__
STATIC int32
_sys_goldengate_ipfix_decode_l2_hashkey(uint8 lchip, ctc_ipfix_data_t* p_data, DsIpfixL2HashKey_m* p_key)
{
    uint32 lport = 0;
    uint8 gchip_id = 0;
    uint32 cmd = 0;
    IpfixL2HashFieldSelect_m  hash_field;
    uint32 hash_sel = 0;
    hw_mac_addr_t             mac_sa   = { 0 };
    hw_mac_addr_t             mac_da   = { 0 };
    uint8 use_cvlan = 0;
    DsPhyPortExt_m phy_port_ext;

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    lport = GetDsIpfixL2HashKey(V, localPhyPort_f, p_key);
    hash_sel = GetDsIpfixL2HashKey(V, flowFieldSel_f , p_key);
     /*gchip_id = SYS_DRV_GPORT_TO_GCHIP(GetDsIpfixL2HashKey(V, globalSrcPort_f, p_key));*/

    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, lport, cmd, &phy_port_ext));
    use_cvlan = GetDsPhyPortExt(V, flowL2KeyUseCvlan_f, &phy_port_ext);

    cmd = DRV_IOR(IpfixL2HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    p_data->dir = GetDsIpfixL2HashKey(V, isEgressKey_f, p_key)?1:0;
    p_data->ether_type = GetDsIpfixL2HashKey(V, etherType_f, p_key);
    p_data->gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    if (use_cvlan)
    {
        p_data->cvlan_cfi = GetDsIpfixL2HashKey(V, stagCfi_f, p_key);
        p_data->cvlan_prio = GetDsIpfixL2HashKey(V, stagCos_f, p_key);
        if (GetDsIpfixL2HashKey(V, svlanIdValid_f, p_key))
        {
            p_data->cvlan = GetDsIpfixL2HashKey(V, svlanId_f, p_key);
            p_data->flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
        }
    }
    else
    {
        p_data->svlan_cfi = GetDsIpfixL2HashKey(V, stagCfi_f, p_key);
        p_data->svlan_prio = GetDsIpfixL2HashKey(V, stagCos_f, p_key);
        if (GetDsIpfixL2HashKey(V, svlanIdValid_f, p_key))
        {
            p_data->svlan = GetDsIpfixL2HashKey(V, svlanId_f, p_key);
            p_data->flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
        }
    }

    if (GetIpfixL2HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        p_data->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsIpfixL2HashKey(V, globalSrcPort_f, p_key));
        p_data->port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }
    else if (GetIpfixL2HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL2HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }
    else if (GetIpfixL2HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL2HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }


    GetDsIpfixL2HashKey(A, macDa_f, p_key, mac_da);
    SYS_GOLDENGATE_SET_USER_MAC(p_data->dst_mac, mac_da);
    GetDsIpfixL2HashKey(A, macSa_f, p_key, mac_sa);
    SYS_GOLDENGATE_SET_USER_MAC(p_data->src_mac, mac_sa);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_decode_l2l3_hashkey(uint8 lchip, ctc_ipfix_data_t* p_data, DsIpfixL2L3HashKey_m* p_key)
{
    uint32 lport = 0;
    uint8 gchip_id = 0;
    uint32 cmd = 0;
    IpfixL2L3HashFieldSelect_m  hash_field;
    uint32 hash_sel = 0;
    hw_mac_addr_t             mac_sa   = { 0 };
    hw_mac_addr_t             mac_da   = { 0 };
    ctc_ipfix_l3_info_t* p_l3_info;
    ctc_ipfix_l4_info_t* p_l4_info;
    uint32 ether_type = 0;
    ctc_ipfix_hash_field_sel_t field_sel;  /*used for get ip mask*/

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    p_l3_info = (ctc_ipfix_l3_info_t*)&(p_data->l3_info);
    p_l4_info = (ctc_ipfix_l4_info_t*)&(p_data->l4_info);

    sal_memset(&field_sel, 0, sizeof(ctc_ipfix_hash_field_sel_t));

    lport = GetDsIpfixL2L3HashKey(V, localPhyPort_f, p_key);
    ether_type = GetDsIpfixL2L3HashKey(V, etherType_f, p_key);
    hash_sel = GetDsIpfixL2L3HashKey(V, flowFieldSel_f , p_key);
     /*gchip_id = SYS_DRV_GPORT_TO_GCHIP(GetDsIpfixL2L3HashKey(V, globalSrcPort_f, p_key));*/

    cmd = DRV_IOR(IpfixL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    p_data->dir = GetDsIpfixL2L3HashKey(V, isEgressKey_f, p_key)?1:0;
    p_data->ether_type = GetDsIpfixL2L3HashKey(V, etherType_f, p_key);

    if (GetDsIpfixL2L3HashKey(V, cvlanIdValid_f, p_key))
    {
        p_data->cvlan = GetDsIpfixL2L3HashKey(V, cvlanId_f, p_key);
        p_data->flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
    }
    p_data->cvlan_prio = GetDsIpfixL2L3HashKey(V, ctagCos_f, p_key);
    p_data->cvlan_cfi = GetDsIpfixL2L3HashKey(V, ctagCfi_f, p_key);

    if (GetDsIpfixL2L3HashKey(V, svlanIdValid_f, p_key))
    {
        p_data->svlan = GetDsIpfixL2L3HashKey(V, svlanId_f, p_key);
        p_data->flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
    }
    p_data->svlan_prio = GetDsIpfixL2L3HashKey(V, stagCos_f, p_key);
    p_data->svlan_cfi = GetDsIpfixL2L3HashKey(V, stagCfi_f, p_key);

    p_data->gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    if (GetIpfixL2L3HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        p_data->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsIpfixL2L3HashKey(V, globalSrcPort_f, p_key));
        p_data->port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }
    else if (GetIpfixL2L3HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL2L3HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }
    else if (GetIpfixL2L3HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL2L3HashKey(V, metadata_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    GetDsIpfixL2L3HashKey(A, macSa_f, p_key, mac_sa);
    GetDsIpfixL2L3HashKey(A, macDa_f, p_key, mac_da);
    SYS_GOLDENGATE_SET_USER_MAC(p_data->src_mac, mac_sa);
    SYS_GOLDENGATE_SET_USER_MAC(p_data->dst_mac, mac_da);

    /* parser l3 information */
    if (ether_type == 0x0800)
    {
        p_l3_info->ipv4.ipsa = GetDsIpfixL2L3HashKey(V, uL3_gIpv4_ipSa_f , p_key);
        p_l3_info->ipv4.ipda = GetDsIpfixL2L3HashKey(V, uL3_gIpv4_ipDa_f , p_key);
        CTC_ERROR_RETURN(sys_goldengate_ipfix_get_hash_field_sel(lchip, hash_sel, CTC_IPFIX_KEY_HASH_L2_L3, &field_sel));
        p_l3_info->ipv4.ipda_masklen = field_sel.u.l2_l3.ip_da_mask;
        p_l3_info->ipv4.ipsa_masklen = field_sel.u.l2_l3.ip_sa_mask;

        p_l3_info->ipv4.ttl = GetDsIpfixL2L3HashKey(V, uL3_gIpv4_ttl_f, p_key);
        p_l3_info->ipv4.dscp = GetDsIpfixL2L3HashKey(V, uL3_gIpv4_dscp_f, p_key);
        p_l3_info->ipv4.ecn = GetDsIpfixL2L3HashKey(V, uL3_gIpv4_ecn_f, p_key);
        p_l3_info->vrfid = GetDsIpfixL2L3HashKey(V, vrfId12to8_f, p_key);
        p_l3_info->vrfid = (p_l3_info->vrfid << 8) +  GetDsIpfixL2L3HashKey(V, uL3_gIpv4_vrfId7to0_f, p_key);
    }
    else if (ether_type == 0x8847 || ether_type == 0x8848)
    {
        p_l3_info->mpls.label_num = GetDsIpfixL2L3HashKey(V, uL3_gMpls_labelNum_f, p_key);
        if (p_l3_info->mpls.label_num > CTC_IPFIX_LABEL_NUM)
        {
            return CTC_E_INVALID_PARAM;
        }

        p_l3_info->mpls.label[0].exp = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsExp0_f, p_key);
        p_l3_info->mpls.label[0].label = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsLabel0_f, p_key);
        p_l3_info->mpls.label[0].sbit = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsSbit0_f, p_key);
        p_l3_info->mpls.label[0].ttl = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsTtl0_f, p_key);
        p_l3_info->mpls.label[1].exp = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsExp1_f, p_key);
        p_l3_info->mpls.label[1].label = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsLabel1_f, p_key);
        p_l3_info->mpls.label[1].sbit = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsSbit1_f, p_key);
        p_l3_info->mpls.label[1].ttl = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsTtl1_f, p_key);
        p_l3_info->mpls.label[2].exp = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsExp2_f, p_key);
        p_l3_info->mpls.label[2].label = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsLabel2_f, p_key);
        p_l3_info->mpls.label[2].sbit = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsSbit2_f, p_key);
        p_l3_info->mpls.label[2].ttl = GetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsTtl2_f, p_key);
    }

    /* process l4 information */

    p_l4_info->aware_tunnel_info_en = GetDsIpfixL2L3HashKey(V, isMergeKey_f, p_key);

    p_l4_info->type.l4_type = GetDsIpfixL2L3HashKey(V, layer4Type_f , p_key);
    /* this check is used for l4_type enum, chip value transfer to api value */
    if (p_l4_info->type.l4_type == 13)
    {
        p_l4_info->type.l4_type = CTC_IPFIX_L4_TYPE_SCTP;
    }

    p_l4_info->l4_sub_type = GetDsIpfixL2L3HashKey(V, layer4UserType_f , p_key);

    if (p_l4_info->aware_tunnel_info_en)
    {
        if (p_l4_info->l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN)
        {
            if (GetIpfixL2L3HashFieldSelect(V, vxlanVniEn_f, &hash_field))
            {
                p_l4_info->vni = GetDsIpfixL2L3HashKey(V, uL4_gVxlan_vni_f, p_key);
            }
        }
        else
        {
            if(GetIpfixL2L3HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                p_l4_info->gre_key = GetDsIpfixL2L3HashKey(V, uL4_gKey_greKey_f, p_key);
            }
        }
    }
    else
    {
        if (p_l4_info->type.l4_type == CTC_IPFIX_L4_TYPE_GRE)
        {
            if(GetIpfixL2L3HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                p_l4_info->gre_key = GetDsIpfixL2L3HashKey(V, uL4_gKey_greKey_f, p_key);
            }
        }
        else if (p_l4_info->type.l4_type == CTC_IPFIX_L4_TYPE_ICMP)
        {
           if (GetIpfixL2L3HashFieldSelect(V, icmpOpcodeEn_f, &hash_field))
           {
               p_l4_info->icmp.icmpcode = GetDsIpfixL2L3HashKey(V, uL4_gPort_l4SourcePort_f, p_key)&0xff;
           }
           if (GetIpfixL2L3HashFieldSelect(V, icmpTypeEn_f, &hash_field))
           {
               p_l4_info->icmp.icmp_type = (GetDsIpfixL2L3HashKey(V, uL4_gPort_l4SourcePort_f, p_key)>>8)&0xff;
           }
        }

        else if ((p_l4_info->type.l4_type == CTC_IPFIX_L4_TYPE_UDP) && (p_l4_info->l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN))
        {
            if (GetIpfixL2L3HashFieldSelect(V, vxlanVniEn_f, &hash_field))
            {
                p_l4_info->vni = GetDsIpfixL2L3HashKey(V, uL4_gVxlan_vni_f, p_key);
            }
        }
        else
        {
            if (GetIpfixL2L3HashFieldSelect(V, l4DestPortEn_f, &hash_field))
            {
                p_l4_info->l4_port.dest_port = GetDsIpfixL2L3HashKey(V, uL4_gPort_l4DestPort_f, p_key);
            }
            if (GetIpfixL2L3HashFieldSelect(V, l4SourcePortEn_f, &hash_field))
            {
                p_l4_info->l4_port.source_port = GetDsIpfixL2L3HashKey(V, uL4_gPort_l4SourcePort_f, p_key);
            }
        }
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_decode_ipv4_hashkey(uint8 lchip, ctc_ipfix_data_t* p_data, DsIpfixL3Ipv4HashKey_m* p_key)
{
    uint32 lport = 0;
    uint8 gchip_id = 0;
    uint32 cmd = 0;
    IpfixL3Ipv4HashFieldSelect_m  hash_field;
    uint32 hash_sel = 0;
    ctc_ipfix_l3_info_t* p_l3_info;
    ctc_ipfix_l4_info_t* p_l4_info;
    ctc_ipfix_hash_field_sel_t field_sel;  /*used for get ip mask*/

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    p_l3_info = (ctc_ipfix_l3_info_t*)&(p_data->l3_info);
    p_l4_info = (ctc_ipfix_l4_info_t*)&(p_data->l4_info);

    sal_memset(&field_sel, 0, sizeof(ctc_ipfix_hash_field_sel_t));

    lport = GetDsIpfixL3Ipv4HashKey(V, localPhyPort_f, p_key);
    hash_sel = GetDsIpfixL3Ipv4HashKey(V, flowFieldSel_f , p_key);
     /*gchip_id = SYS_DRV_GPORT_TO_GCHIP(GetDsIpfixL3Ipv4HashKey(V, globalSrcPort_f, p_key));*/

    cmd = DRV_IOR(IpfixL3Ipv4HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    p_data->dir = GetDsIpfixL3Ipv4HashKey(V, isEgressKey_f, p_key)?1:0;
    p_data->gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    if (GetIpfixL3Ipv4HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        p_data->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsIpfixL3Ipv4HashKey(V, globalSrcPort_f, p_key));
        p_data->port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }
    else if (GetIpfixL3Ipv4HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL3Ipv4HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }
    else if (GetIpfixL3Ipv4HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL3Ipv4HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    p_l3_info->ipv4.dscp = GetDsIpfixL3Ipv4HashKey(V, dscp_f , p_key);
    p_l3_info->ipv4.ipsa = GetDsIpfixL3Ipv4HashKey(V, ipSa_f , p_key);
    p_l3_info->ipv4.ipda = GetDsIpfixL3Ipv4HashKey(V, ipDa_f , p_key);
    CTC_ERROR_RETURN(sys_goldengate_ipfix_get_hash_field_sel(lchip, hash_sel, CTC_IPFIX_KEY_HASH_IPV4, &field_sel));
    p_l3_info->ipv4.ipda_masklen = field_sel.u.ipv4.ip_da_mask;
    p_l3_info->ipv4.ipsa_masklen = field_sel.u.ipv4.ip_sa_mask;

    /* process l4 information */

    p_l4_info->aware_tunnel_info_en = GetDsIpfixL3Ipv4HashKey(V, isMergeKey_f, p_key);
    p_l4_info->type.ip_protocol = GetDsIpfixL3Ipv4HashKey(V, layer3HeaderProtocol_f, p_key);
    p_l4_info->l4_sub_type = GetDsIpfixL3Ipv4HashKey(V, layer4UserType_f, p_key);

    if (p_l4_info->aware_tunnel_info_en)
    {
        if (p_l4_info->l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN)
        {
           if (GetIpfixL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &hash_field))
           {
               p_l4_info->vni = GetDsIpfixL3Ipv4HashKey(V, uL4_gVxlan_vni_f, p_key);
           }
        }
        else
        {
            if(GetIpfixL3Ipv4HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                p_l4_info->gre_key = GetDsIpfixL3Ipv4HashKey(V, uL4_gKey_greKey_f, p_key);
            }
        }
    }
    else
    {
        if (p_l4_info->type.ip_protocol == SYS_IPFIX_GRE_PROTOCOL)
        {
            if(GetIpfixL3Ipv4HashFieldSelect(V, greKeyEn_f, &hash_field))
            {
                p_l4_info->gre_key = GetDsIpfixL3Ipv4HashKey(V, uL4_gKey_greKey_f, p_key);
            }
        }
        else if (p_l4_info->type.ip_protocol == SYS_IPFIX_ICMP_PROTOCOL)
        {
           if (GetIpfixL3Ipv4HashFieldSelect(V, icmpOpcodeEn_f, &hash_field))
           {
               p_l4_info->icmp.icmpcode = GetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4SourcePort_f, p_key)&0xff;
           }
           if (GetIpfixL3Ipv4HashFieldSelect(V, icmpTypeEn_f, &hash_field))
           {
               p_l4_info->icmp.icmp_type = (GetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4SourcePort_f, p_key)>>8)&0xff;
           }
        }
        else if ((p_l4_info->type.ip_protocol == SYS_IPFIX_UDP_PROTOCOL) && (p_l4_info->l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN))
        {
           if (GetIpfixL3Ipv4HashFieldSelect(V, vxlanVniEn_f, &hash_field))
           {
               p_l4_info->vni = GetDsIpfixL3Ipv4HashKey(V, uL4_gVxlan_vni_f, p_key);
           }
        }
        else
        {
            if (GetIpfixL3Ipv4HashFieldSelect(V, l4DestPortEn_f, &hash_field))
            {
                p_l4_info->l4_port.dest_port = GetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4DestPort_f, p_key);
            }
            if (GetIpfixL3Ipv4HashFieldSelect(V, l4SourcePortEn_f, &hash_field))
            {
                p_l4_info->l4_port.source_port = GetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4SourcePort_f, p_key);
            }
        }
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_goldengate_ipfix_decode_ipv6_hashkey(uint8 lchip, ctc_ipfix_data_t* p_data, DsIpfixL3Ipv6HashKey_m* p_key)
{
    uint32 lport = 0;
    uint8 gchip_id = 0;
    uint32 cmd = 0;
    IpfixL3Ipv6HashFieldSelect_m  hash_field;
    uint32 hash_sel = 0;
    ctc_ipfix_l3_info_t* p_l3_info;
    ctc_ipfix_l4_info_t* p_l4_info;
    ctc_ipfix_hash_field_sel_t field_sel;  /*used for get ip mask*/
    uint32 ipv6_address[4] = {0, 0, 0, 0};

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    p_l3_info = (ctc_ipfix_l3_info_t*)&(p_data->l3_info);
    p_l4_info = (ctc_ipfix_l4_info_t*)&(p_data->l4_info);

    sal_memset(&field_sel, 0, sizeof(ctc_ipfix_hash_field_sel_t));

    lport = GetDsIpfixL3Ipv6HashKey(V, localPhyPort_f, p_key);
    hash_sel = GetDsIpfixL3Ipv6HashKey(V, flowFieldSel_f , p_key);
     /*gchip_id = SYS_DRV_GPORT_TO_GCHIP(GetDsIpfixL3Ipv6HashKey(V, globalSrcPort_f, p_key));*/

    cmd = DRV_IOR(IpfixL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    p_data->dir = GetDsIpfixL3Ipv6HashKey(V, isEgressKey_f, p_key)?1:0;
    p_data->gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    p_data->l3_info.ipv6.dscp = GetDsIpfixL3Ipv6HashKey(V, dscp_f, p_key);

    if (GetIpfixL3Ipv6HashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        p_data->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsIpfixL3Ipv6HashKey(V, globalSrcPort_f, p_key));
        p_data->port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }
    else if (GetIpfixL3Ipv6HashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL3Ipv6HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }
    else if (GetIpfixL3Ipv6HashFieldSelect(V, metadataEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL3Ipv6HashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    GetDsIpfixL3Ipv6HashKey(A, ipSa_f , p_key,  ipv6_address);
    p_l3_info->ipv6.ipsa[0] = ipv6_address[3];
    p_l3_info->ipv6.ipsa[1] = ipv6_address[2];
    p_l3_info->ipv6.ipsa[2] = ipv6_address[1];
    p_l3_info->ipv6.ipsa[3] = ipv6_address[0];

    GetDsIpfixL3Ipv6HashKey(A, ipDa_f , p_key,  ipv6_address);
    p_l3_info->ipv6.ipda[0] = ipv6_address[3];
    p_l3_info->ipv6.ipda[1] = ipv6_address[2];
    p_l3_info->ipv6.ipda[2] = ipv6_address[1];
    p_l3_info->ipv6.ipda[3] = ipv6_address[0];

    CTC_ERROR_RETURN(sys_goldengate_ipfix_get_hash_field_sel(lchip, hash_sel, CTC_IPFIX_KEY_HASH_IPV6, &field_sel));
    p_l3_info->ipv6.ipda_masklen = field_sel.u.ipv6.ip_da_mask;
    p_l3_info->ipv6.ipsa_masklen = field_sel.u.ipv6.ip_sa_mask;

    /* parser l4 info */

    p_l4_info->aware_tunnel_info_en = GetDsIpfixL3Ipv6HashKey(V, isMergeKey_f, p_key);

    p_l4_info->type.l4_type = GetDsIpfixL3Ipv6HashKey(V, layer4Type_f , p_key);
    /* this check is used for l4_type enum, chip value transfer to api value */
    if (p_l4_info->type.l4_type == 13)
    {
        p_l4_info->type.l4_type = CTC_IPFIX_L4_TYPE_SCTP;
    }

    if(GetDsIpfixL3Ipv6HashKey(V, isVxlan_f , p_key))
    {
        p_l4_info->l4_sub_type = CTC_IPFIX_L4_SUB_TYPE_VXLAN;
    }

    if (GetIpfixL3Ipv6HashFieldSelect(V, ipv6FlowLabelEn_f, &hash_field))
    {
        p_data->l3_info.ipv6.flow_label = GetDsIpfixL3Ipv6HashKey(V, uL4_gKey_greKey_f, p_key);
    }
    else
    {
        if (p_l4_info->aware_tunnel_info_en)
        {
            if (GetIpfixL3Ipv6HashFieldSelect(V, isVxlanEn_f, &hash_field))
            {
                if (GetIpfixL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &hash_field))
                {
                    p_l4_info->vni = GetDsIpfixL3Ipv6HashKey(V, uL4_gVxlan_vni_f, p_key);
                }
            }
            else
            {
                if(GetIpfixL2L3HashFieldSelect(V, greKeyEn_f, &hash_field))
                {
                    p_l4_info->gre_key = GetDsIpfixL2L3HashKey(V, uL4_gKey_greKey_f, p_key);
                }
            }
        }
        else
        {
            if (p_l4_info->type.l4_type == CTC_IPFIX_L4_TYPE_GRE)
            {
                if(GetIpfixL2L3HashFieldSelect(V, greKeyEn_f, &hash_field))
                {
                    p_l4_info->gre_key = GetDsIpfixL2L3HashKey(V, uL4_gKey_greKey_f, p_key);
                }
            }
            else if (p_l4_info->type.l4_type == CTC_IPFIX_L4_TYPE_ICMP)
            {
               if (GetIpfixL3Ipv6HashFieldSelect(V, icmpOpcodeEn_f, &hash_field))
               {
                   p_l4_info->icmp.icmpcode = GetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4SourcePort_f, p_key)&0xff;
               }
               if (GetIpfixL3Ipv6HashFieldSelect(V, icmpTypeEn_f, &hash_field))
               {
                   p_l4_info->icmp.icmp_type = (GetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4SourcePort_f, p_key)>>8)&0xff;
               }
            }
            else if ((p_l4_info->type.l4_type == CTC_IPFIX_L4_TYPE_UDP) && GetIpfixL3Ipv6HashFieldSelect(V, isVxlanEn_f, &hash_field))
            {
                if (GetIpfixL3Ipv6HashFieldSelect(V, vxlanVniEn_f, &hash_field))
                {
                    p_l4_info->vni = GetDsIpfixL3Ipv6HashKey(V, uL4_gVxlan_vni_f, p_key);
                }
            }
            else
            {
                if (GetIpfixL3Ipv6HashFieldSelect(V, l4DestPortEn_f, &hash_field))
                {
                    p_l4_info->l4_port.dest_port = GetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4DestPort_f, p_key);
                }
                if (GetIpfixL3Ipv6HashFieldSelect(V, l4SourcePortEn_f, &hash_field))
                {
                    p_l4_info->l4_port.source_port = GetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4SourcePort_f, p_key);
                }
            }
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_decode_mpls_hashkey(uint8 lchip, ctc_ipfix_data_t* p_data, DsIpfixL3MplsHashKey_m* p_key)
{
    uint32 lport = 0;
    uint8 gchip_id = 0;
    uint32 cmd = 0;
    IpfixL3MplsHashFieldSelect_m  hash_field;
    uint32 hash_sel = 0;
    ctc_ipfix_l3_info_t* p_l3_info;

    sys_goldengate_get_gchip_id(lchip, &gchip_id);
    p_l3_info = (ctc_ipfix_l3_info_t*)&(p_data->l3_info);
    lport = GetDsIpfixL3MplsHashKey(V, localPhyPort_f, p_key);
    hash_sel = GetDsIpfixL3MplsHashKey(V, flowFieldSel_f , p_key);
     /*gchip_id = SYS_DRV_GPORT_TO_GCHIP(GetDsIpfixL3MplsHashKey(V, globalSrcPort_f, p_key));*/

    cmd = DRV_IOR(IpfixL3MplsHashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, hash_sel, cmd, &hash_field));

    p_data->dir = GetDsIpfixL3MplsHashKey(V, isEgressKey_f, p_key)?1:0;
    p_data->gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip_id, lport);

    if (GetIpfixL3MplsHashFieldSelect(V, globalSrcPortEn_f, &hash_field))
    {
        p_data->gport = SYS_MAP_DRV_GPORT_TO_CTC_GPORT(GetDsIpfixL3MplsHashKey(V, globalSrcPort_f, p_key));
        p_data->port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }
    else if (GetIpfixL3MplsHashFieldSelect(V, logicSrcPortEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL3MplsHashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }
    else if (GetIpfixL3MplsHashFieldSelect(V, metadataEn_f, &hash_field))
    {
        p_data->logic_port = GetDsIpfixL3MplsHashKey(V, globalSrcPort_f, p_key);
        p_data->port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    p_l3_info->mpls.label_num = GetDsIpfixL3MplsHashKey(V, labelNum_f, p_key);
    if (p_l3_info->mpls.label_num > CTC_IPFIX_LABEL_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    p_l3_info->mpls.label[0].exp = GetDsIpfixL3MplsHashKey(V, mplsExp0_f, p_key);
    p_l3_info->mpls.label[0].label = GetDsIpfixL3MplsHashKey(V, mplsLabel0_f, p_key);
    p_l3_info->mpls.label[0].sbit = GetDsIpfixL3MplsHashKey(V, mplsSbit0_f, p_key);
    p_l3_info->mpls.label[0].ttl = GetDsIpfixL3MplsHashKey(V, mplsTtl0_f, p_key);
    p_l3_info->mpls.label[1].exp = GetDsIpfixL3MplsHashKey(V, mplsExp1_f, p_key);
    p_l3_info->mpls.label[1].label = GetDsIpfixL3MplsHashKey(V, mplsLabel1_f, p_key);
    p_l3_info->mpls.label[1].sbit = GetDsIpfixL3MplsHashKey(V, mplsSbit1_f, p_key);
    p_l3_info->mpls.label[1].ttl = GetDsIpfixL3MplsHashKey(V, mplsTtl1_f, p_key);
    p_l3_info->mpls.label[2].exp = GetDsIpfixL3MplsHashKey(V, mplsExp2_f, p_key);
    p_l3_info->mpls.label[2].label = GetDsIpfixL3MplsHashKey(V, mplsLabel2_f, p_key);
    p_l3_info->mpls.label[2].sbit = GetDsIpfixL3MplsHashKey(V, mplsSbit2_f, p_key);
    p_l3_info->mpls.label[2].ttl = GetDsIpfixL3MplsHashKey(V, mplsTtl2_f, p_key);

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_decode_ad_info(uint8 lchip, ctc_ipfix_data_t* p_data, DsIpfixSessionRecord_m* p_ad)
{
    uint32 byte_cnt[2] = {0};
    uint32 dest_type = 0;
    uint8 dest_chip= 0;

    GetDsIpfixSessionRecord(A, byteCount_f , p_ad, byte_cnt);
    p_data->byte_count = 0;
    p_data->byte_count = byte_cnt[1];
    p_data->byte_count <<= 32;
    p_data->byte_count |= byte_cnt[0];
    dest_type = GetDsIpfixSessionRecord(V, destinationType_f, p_ad);
    p_data->export_reason = GetDsIpfixSessionRecord(V, exportReason_f , p_ad);
    p_data->start_timestamp = GetDsIpfixSessionRecord(V, firstTs_f , p_ad);
    p_data->last_timestamp = GetDsIpfixSessionRecord(V, lastTs_f , p_ad);
    p_data->max_ttl = GetDsIpfixSessionRecord(V, maxTtl_f , p_ad);
    p_data->min_ttl = GetDsIpfixSessionRecord(V, minTtl_f , p_ad);

    if (GetDsIpfixSessionRecord(V, fragment_f , p_ad))
    {
        p_data->flags |= CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED;
    }
    if (GetDsIpfixSessionRecord(V, droppedPacket_f  , p_ad))
    {
        p_data->flags |= CTC_IPFIX_DATA_DROP_DETECTED;
    }
    if (GetDsIpfixSessionRecord(V, ttlChanged_f   , p_ad))
    {
        p_data->flags |= CTC_IPFIX_DATA_TTL_CHANGE;
    }

    p_data->pkt_count = GetDsIpfixSessionRecord(V, packetCount_f , p_ad);
    p_data->ttl = GetDsIpfixSessionRecord(V, ttl_f , p_ad);

    switch(dest_type)
    {
        case SYS_IPFIX_UNICAST_DEST:
            dest_chip = GetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_ad);
            p_data->dest_gport = GetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToNormal_destId_f , p_ad);
            p_data->dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip,  p_data->dest_gport);
            break;
        case SYS_IPFIX_LINKAGG_DEST:
            p_data->dest_group_id = GetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f , p_ad);
            p_data->flags |= CTC_IPFIX_DATA_LINKAGG_DETECTED;
            break;
        case SYS_IPFIX_L2MC_DEST:
            p_data->dest_group_id = GetDsIpfixSessionRecord(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , p_ad);
            p_data->flags |= CTC_IPFIX_DATA_L2_MCAST_DETECTED;
            break;
        case SYS_IPFIX_L3MC_DEST:
            p_data->dest_group_id = GetDsIpfixSessionRecord(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , p_ad);
            p_data->flags |= CTC_IPFIX_DATA_L3_MCAST_DETECTED;
            break;
        case SYS_IPFIX_BCAST_DEST:
            p_data->dest_group_id = GetDsIpfixSessionRecord(V, uDestinationInfo_gBcast_floodingId_f, p_ad);
            p_data->flags |= CTC_IPFIX_DATA_BCAST_DETECTED;
            break;
        case SYS_IPFIX_UNKNOW_PKT_DEST:
            p_data->dest_group_id = GetDsIpfixSessionRecord(V, uDestinationInfo_gUnknownPkt_floodingId_f, p_ad);
            p_data->flags |= CTC_IPFIX_DATA_UNKNOW_PKT_DETECTED;
            break;
        case SYS_IPFIX_APS_DEST:
            p_data->dest_group_id = GetDsIpfixSessionRecord(V, uDestinationInfo_gApsGroup_apsGroupId_f , p_ad);
            p_data->flags |= CTC_IPFIX_DATA_APS_DETECTED;
            break;
        case SYS_IPFIX_ECMP_DEST:
            dest_chip = GetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToNormal_destChipId_f , p_ad);
            p_data->dest_gport = GetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToNormal_destId_f , p_ad);
            p_data->dest_gport = SYS_MAP_DRV_LPORT_TO_CTC_GPORT(dest_chip,  p_data->dest_gport);
            p_data->flags |= CTC_IPFIX_DATA_ECMP_DETECTED;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_decode_hashkey_ad(uint8 lchip, ctc_ipfix_data_t* p_data, uint8 is_ad, void* p_key)
{
    uint32 type = p_data->key_type;

    if (is_ad)
    {
        CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_ad_info(lchip, p_data, (DsIpfixSessionRecord_m*)p_key));
    }
    else
    {
        switch(type)
        {
            case CTC_IPFIX_KEY_HASH_MAC:
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_l2_hashkey(lchip, p_data, (DsIpfixL2HashKey_m*)p_key));
                break;
            case CTC_IPFIX_KEY_HASH_IPV4:
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_ipv4_hashkey(lchip, p_data, (DsIpfixL3Ipv4HashKey_m*)p_key));
                break;
            case CTC_IPFIX_KEY_HASH_MPLS:
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_mpls_hashkey(lchip, p_data, (DsIpfixL3MplsHashKey_m*)p_key));
                break;
            case CTC_IPFIX_KEY_HASH_IPV6:
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_ipv6_hashkey(lchip, p_data, (DsIpfixL3Ipv6HashKey_m*)p_key));
                break;
            case CTC_IPFIX_KEY_HASH_L2_L3:
                CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_l2l3_hashkey(lchip, p_data, (DsIpfixL2L3HashKey_m*)p_key));
                break;
            default:
                return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_l2_hashkey(uint8 lchip, ctc_ipfix_data_t* p_key, void* p_data)
{
    DsIpfixL2HashKey_m l2_key;
    DsPhyPortExt_m phy_port_ext;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;
    IpfixL2HashFieldSelect_m  hash_field;
    uint32  mac[2] = {0};
    uint16 src_port = 0;
    uint16 src_lport = 0;
    uint32 cmd = 0;
    uint8 use_cvlan = 0;
    uint32 field_sel_id = 0;
    uint32 vlan = 0;
    uint16 cfi;
    uint16 cos;

    sal_memset(&l2_key, 0, sizeof(l2_key));

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_key->gport, lchip, src_lport);

    cmd = DRV_IOR(DsPhyPortExt_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &phy_port_ext));

    use_cvlan = GetDsPhyPortExt(V, flowL2KeyUseCvlan_f, &phy_port_ext);

    /* process gport, logic-port, metadata */
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_GPORT)
    {
        CTC_GLOBAL_PORT_CHECK(p_key->gport);
        src_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_key->gport);
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_LOGIC_PORT)
    {
        SYS_LOGIC_PORT_CHECK(p_key->logic_port);
        src_port = p_key->logic_port;
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        CTC_MAX_VALUE_CHECK(p_key->logic_port, 0x3FFF);
        src_port = p_key->logic_port;
    }

    if (p_key->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_src_port));
        field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_dest_port));
        field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
    }

    SetDsIpfixL2HashKey(V, hashKeyType_f, &l2_key, SYS_IPFIX_HASH_TYPE_L2);
    SetDsIpfixL2HashKey(V, localPhyPort_f, &l2_key, src_lport);

    /*TODO, Notice:if using SCL modify flowFieldSel, cannot get from port,
       should do lookup from scl module to get flowfieldSel */
    SetDsIpfixL2HashKey(V, flowFieldSel_f, &l2_key, field_sel_id);
    SetDsIpfixL2HashKey(V, isEgressKey_f, &l2_key, ((p_key->dir==CTC_EGRESS)?1:0));

    cmd = DRV_IOR(IpfixL2HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &hash_field));

    SetDsIpfixL2HashKey(V, etherType_f, &l2_key, p_key->ether_type);
    SetDsIpfixL2HashKey(V, globalSrcPort_f, &l2_key, src_port);
    SetDsIpfixL2HashKey(V, isMergeKey_f, &l2_key, 0);

    /*whether use cvlan need retrieve from DsPhyPortExt, TODO later need get from port module*/
    SetDsIpfixL2HashKey(V, flowL2KeyUseCvlan_f, &l2_key, (use_cvlan)?1:0);
    vlan = use_cvlan?p_key->cvlan:p_key->svlan;
    cfi = use_cvlan?p_key->cvlan_cfi:p_key->svlan_cfi;
    cos = use_cvlan?p_key->cvlan_prio:p_key->svlan_prio;
    if ((p_key->flags&CTC_IPFIX_DATA_CVLAN_TAGGED)||(p_key->flags&CTC_IPFIX_DATA_SVLAN_TAGGED))
    {
        SetDsIpfixL2HashKey(V, svlanIdValid_f, &l2_key, 1);
    }
    SetDsIpfixL2HashKey(V, svlanId_f, &l2_key, vlan);
    SetDsIpfixL2HashKey(V, stagCfi_f, &l2_key, cfi);
    SetDsIpfixL2HashKey(V, stagCos_f, &l2_key, cos);

    SYS_GOLDENGATE_SET_HW_MAC(mac, p_key->dst_mac);
    SetDsIpfixL2HashKey(A, macDa_f, &l2_key, mac);
    SYS_GOLDENGATE_SET_HW_MAC(mac, p_key->src_mac);
    SetDsIpfixL2HashKey(A, macSa_f, &l2_key, mac);

    sal_memcpy((uint8*)p_data, (uint8*)&l2_key, sizeof(l2_key));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_l2l3_hashkey(uint8 lchip, ctc_ipfix_data_t* p_key, void* p_data)
{
    DsIpfixL2L3HashKey_m l2l3_key;
    IpfixL2L3HashFieldSelect_m  hash_field;
    uint32  mac[2] = {0};
    uint16 src_port = 0;
    uint16 src_lport = 0;
    uint32 cmd = 0;
    uint16 l4_srcport = 0;
    uint32 field_sel_id = 0;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;

    /* check l4 info para*/
    if (p_key->l4_info.type.l4_type > 0xF)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_key->l4_info.l4_sub_type >= CTC_IPFIX_L4_SUB_TYPE_MAX)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /* this check is used for l4_type enum, API value transfer to chip value */
    if (p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_SCTP)
    {
        p_key->l4_info.type.l4_type =13;
    }

    sal_memset(&l2l3_key, 0, sizeof(l2l3_key));

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_key->gport, lchip, src_lport);

    /* process gport, logic-port, metadata */
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_GPORT)
    {
        CTC_GLOBAL_PORT_CHECK(p_key->gport);
        src_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_key->gport);
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_LOGIC_PORT)
    {
        SYS_LOGIC_PORT_CHECK(p_key->logic_port);
        src_port = p_key->logic_port;
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        CTC_MAX_VALUE_CHECK(p_key->logic_port, 0x3FFF);
        src_port = p_key->logic_port;
    }

    if (p_key->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_src_port));
        field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_dest_port));
        field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
    }

    SetDsIpfixL2L3HashKey(V, hashKeyType0_f, &l2l3_key, SYS_IPFIX_HASH_TYPE_L2L3);
    SetDsIpfixL2L3HashKey(V, hashKeyType1_f, &l2l3_key, SYS_IPFIX_HASH_TYPE_L2L3);
    SetDsIpfixL2L3HashKey(V, localPhyPort_f, &l2l3_key, src_lport);
    SetDsIpfixL2L3HashKey(V, flowFieldSel_f, &l2l3_key, field_sel_id);
    SetDsIpfixL2L3HashKey(V, isEgressKey_f, &l2l3_key, ((p_key->dir==CTC_EGRESS)?1:0));

    cmd = DRV_IOR( IpfixL2L3HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &hash_field));
    SetDsIpfixL2L3HashKey(V, etherType_f, &l2l3_key, p_key->ether_type);

    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        SetDsIpfixL2L3HashKey(V, metadata_f, &l2l3_key, src_port);
    }
    else
    {
        SetDsIpfixL2L3HashKey(V, globalSrcPort_f, &l2l3_key, src_port);
    }

    SetDsIpfixL2L3HashKey(V, isMergeKey_f, &l2l3_key, p_key->l4_info.aware_tunnel_info_en);

     /*cvlan*/
    if(p_key->flags&CTC_IPFIX_DATA_CVLAN_TAGGED)
    {
        SetDsIpfixL2L3HashKey(V, cvlanIdValid_f, &l2l3_key, 1);
    }
    SetDsIpfixL2L3HashKey(V, cvlanId_f , &l2l3_key, p_key->cvlan);
    SetDsIpfixL2L3HashKey(V, ctagCfi_f , &l2l3_key, p_key->cvlan_cfi);
    SetDsIpfixL2L3HashKey(V, ctagCos_f , &l2l3_key, p_key->cvlan_prio);

     /*svlan*/
    if(p_key->flags&CTC_IPFIX_DATA_SVLAN_TAGGED)
    {
        SetDsIpfixL2L3HashKey(V, svlanIdValid_f , &l2l3_key, 1);
    }
    SetDsIpfixL2L3HashKey(V, svlanId_f , &l2l3_key, p_key->svlan);
    SetDsIpfixL2L3HashKey(V, stagCfi_f , &l2l3_key, p_key->svlan_cfi);
    SetDsIpfixL2L3HashKey(V, stagCos_f , &l2l3_key, p_key->svlan_prio);

    SYS_GOLDENGATE_SET_HW_MAC(mac, p_key->dst_mac);
    SetDsIpfixL2L3HashKey(A, macDa_f, &l2l3_key, mac);
    SYS_GOLDENGATE_SET_HW_MAC(mac, p_key->src_mac);
    SetDsIpfixL2L3HashKey(A, macSa_f, &l2l3_key, mac);

    /*gen l3 key*/
    if (p_key->ether_type == 0x0800)
    {
        if (p_key->l3_info.ipv4.ipda != 0)
        {
            IPFIX_IPV4_MASK_LEN_CHECK(p_key->l3_info.ipv4.ipda_masklen);
            IPFIX_IPV4_MASK(p_key->l3_info.ipv4.ipda, p_key->l3_info.ipv4.ipda_masklen);
            SetDsIpfixL2L3HashKey(V, uL3_gIpv4_ipDa_f, &l2l3_key, p_key->l3_info.ipv4.ipda);
        }
        if (p_key->l3_info.ipv4.ipsa != 0)
        {
            IPFIX_IPV4_MASK_LEN_CHECK(p_key->l3_info.ipv4.ipsa_masklen);
            IPFIX_IPV4_MASK(p_key->l3_info.ipv4.ipsa, p_key->l3_info.ipv4.ipsa_masklen);
            SetDsIpfixL2L3HashKey(V, uL3_gIpv4_ipSa_f, &l2l3_key, p_key->l3_info.ipv4.ipsa);
        }

        SetDsIpfixL2L3HashKey(V, uL3_gIpv4_ttl_f, &l2l3_key, p_key->l3_info.ipv4.ttl);
        SetDsIpfixL2L3HashKey(V, uL3_gIpv4_dscp_f, &l2l3_key, p_key->l3_info.ipv4.dscp);
        SetDsIpfixL2L3HashKey(V, uL3_gIpv4_ecn_f, &l2l3_key, p_key->l3_info.ipv4.ecn);

        SetDsIpfixL2L3HashKey(V, uL3_gIpv4_vrfId7to0_f, &l2l3_key, (p_key->l3_info.vrfid&0xff));
        SetDsIpfixL2L3HashKey(V, vrfId12to8_f, &l2l3_key, ((p_key->l3_info.vrfid>>8)&0x1f));
    }
    else if (p_key->ether_type == 0x8847 || p_key->ether_type == 0x8848)
    {
        if (p_key->l3_info.mpls.label_num > CTC_IPFIX_LABEL_NUM)
        {
            return CTC_E_INVALID_PARAM;
        }
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_labelNum_f, &l2l3_key, p_key->l3_info.mpls.label_num);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsExp0_f, &l2l3_key, p_key->l3_info.mpls.label[0].exp);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsLabel0_f, &l2l3_key, p_key->l3_info.mpls.label[0].label);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsSbit0_f, &l2l3_key, p_key->l3_info.mpls.label[0].sbit);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsTtl0_f, &l2l3_key, p_key->l3_info.mpls.label[0].ttl);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsExp1_f, &l2l3_key, p_key->l3_info.mpls.label[1].exp);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsLabel1_f, &l2l3_key, p_key->l3_info.mpls.label[1].label);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsSbit1_f, &l2l3_key, p_key->l3_info.mpls.label[1].sbit);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsTtl1_f, &l2l3_key, p_key->l3_info.mpls.label[1].ttl);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsExp2_f, &l2l3_key, p_key->l3_info.mpls.label[2].exp);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsLabel2_f, &l2l3_key, p_key->l3_info.mpls.label[2].label);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsSbit2_f, &l2l3_key, p_key->l3_info.mpls.label[2].sbit);
        SetDsIpfixL2L3HashKey(V, uL3_gMpls_mplsTtl2_f, &l2l3_key, p_key->l3_info.mpls.label[2].ttl);
    }

    /*gen l4 key*/
    SetDsIpfixL2L3HashKey(V, layer4Type_f, &l2l3_key, p_key->l4_info.type.l4_type);
    SetDsIpfixL2L3HashKey(V, layer4UserType_f, &l2l3_key, p_key->l4_info.l4_sub_type);

    if (p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_ICMP)
    {
        l4_srcport = (p_key->l4_info.icmp.icmpcode) & 0xff;
        l4_srcport |=  (((p_key->l4_info.icmp.icmp_type) & 0xff)<<8);
        SetDsIpfixL2L3HashKey(V, uL4_gPort_l4SourcePort_f, &l2l3_key, l4_srcport);
    }
    else if (p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_GRE)
    {
        SetDsIpfixL2L3HashKey(V, uL4_gKey_greKey_f, &l2l3_key, p_key->l4_info.gre_key);
    }
    else if ((p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_UDP) && (p_key->l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN))
    {
        SetDsIpfixL2L3HashKey(V, uL4_gVxlan_vni_f, &l2l3_key, p_key->l4_info.vni);
    }
    else
    {
        SetDsIpfixL2L3HashKey(V, uL4_gPort_l4DestPort_f, &l2l3_key, p_key->l4_info.l4_port.dest_port);
        SetDsIpfixL2L3HashKey(V, uL4_gPort_l4SourcePort_f, &l2l3_key, p_key->l4_info.l4_port.source_port);
    }

    sal_memcpy((uint8*)p_data, (uint8*)&l2l3_key, sizeof(l2l3_key));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_ipv4_hashkey(uint8 lchip, ctc_ipfix_data_t* p_key, void* p_data)
{
    DsIpfixL3Ipv4HashKey_m ipv4_key;
    uint16 src_port = 0;
    uint16 src_lport = 0;
    uint16 l4_srcport = 0;
    uint32 field_sel_id = 0;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;
    uint32 cmd = 0;

    sal_memset(&ipv4_key, 0, sizeof(ipv4_key));

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_key->gport, lchip, src_lport);

    /* process gport, logic-port, metadata */
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_GPORT)
    {
        CTC_GLOBAL_PORT_CHECK(p_key->gport);
        src_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_key->gport);
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_LOGIC_PORT)
    {
        SYS_LOGIC_PORT_CHECK(p_key->logic_port);
        src_port = p_key->logic_port;
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        CTC_MAX_VALUE_CHECK(p_key->logic_port, 0x3FFF);
        src_port = p_key->logic_port;
    }

    if (p_key->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_src_port));
        field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_dest_port));
        field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
    }

    SetDsIpfixL3Ipv4HashKey(V, hashKeyType_f, &ipv4_key, SYS_IPFIX_HASH_TYPE_IPV4);
    SetDsIpfixL3Ipv4HashKey(V, localPhyPort_f, &ipv4_key, src_lport);
    SetDsIpfixL3Ipv4HashKey(V, flowFieldSel_f, &ipv4_key, field_sel_id);
    SetDsIpfixL3Ipv4HashKey(V, isEgressKey_f, &ipv4_key, ((p_key->dir==CTC_EGRESS)?1:0));
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        SetDsIpfixL3Ipv4HashKey(V, metadata_f, &ipv4_key, src_port);
    }
    else
    {
        SetDsIpfixL3Ipv4HashKey(V, globalSrcPort_f, &ipv4_key, src_port);
    }
    SetDsIpfixL3Ipv4HashKey(V, dscp_f, &ipv4_key, p_key->l3_info.ipv4.dscp);

    /*gen l3 key*/
    if (p_key->l3_info.ipv4.ipda != 0)
    {
        IPFIX_IPV4_MASK_LEN_CHECK(p_key->l3_info.ipv4.ipda_masklen);
        IPFIX_IPV4_MASK(p_key->l3_info.ipv4.ipda, p_key->l3_info.ipv4.ipda_masklen);
        SetDsIpfixL3Ipv4HashKey(V, ipDa_f, &ipv4_key, p_key->l3_info.ipv4.ipda);
    }
    if (p_key->l3_info.ipv4.ipsa != 0)
    {
        IPFIX_IPV4_MASK_LEN_CHECK(p_key->l3_info.ipv4.ipsa_masklen);
        IPFIX_IPV4_MASK(p_key->l3_info.ipv4.ipsa, p_key->l3_info.ipv4.ipsa_masklen);
        SetDsIpfixL3Ipv4HashKey(V, ipSa_f, &ipv4_key, p_key->l3_info.ipv4.ipsa);
    }

    /*gen l4 key*/
    SetDsIpfixL3Ipv4HashKey(V, layer3HeaderProtocol_f, &ipv4_key, p_key->l4_info.type.ip_protocol);
    SetDsIpfixL3Ipv4HashKey(V, layer4UserType_f, &ipv4_key, p_key->l4_info.l4_sub_type);

    SetDsIpfixL3Ipv4HashKey(V, isMergeKey_f, &ipv4_key, p_key->l4_info.aware_tunnel_info_en);

    if (p_key->l4_info.type.ip_protocol == SYS_IPFIX_ICMP_PROTOCOL)
    {
        l4_srcport = (p_key->l4_info.icmp.icmpcode) & 0xff;
        l4_srcport |=  (((p_key->l4_info.icmp.icmp_type) & 0xff)<<8);
        SetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4SourcePort_f, &ipv4_key, l4_srcport);
    }
    else if (p_key->l4_info.type.ip_protocol == SYS_IPFIX_GRE_PROTOCOL)
    {
        SetDsIpfixL3Ipv4HashKey(V, uL4_gKey_greKey_f, &ipv4_key, p_key->l4_info.gre_key);
    }
    else if ((p_key->l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN) && (p_key->l4_info.type.ip_protocol == SYS_IPFIX_UDP_PROTOCOL))
    {
        SetDsIpfixL3Ipv4HashKey(V, uL4_gVxlan_vni_f, &ipv4_key, p_key->l4_info.vni);
    }
    else
    {
        SetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4DestPort_f, &ipv4_key, p_key->l4_info.l4_port.dest_port);
        SetDsIpfixL3Ipv4HashKey(V, uL4_gPort_l4SourcePort_f, &ipv4_key, p_key->l4_info.l4_port.source_port);
    }

    sal_memcpy((uint8*)p_data, (uint8*)&ipv4_key, sizeof(ipv4_key));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_mpls_hashkey(uint8 lchip, ctc_ipfix_data_t* p_key, void* p_data)
{
    DsIpfixL3MplsHashKey_m mpls_key;
    uint16 src_port = 0;
    uint16 src_lport = 0;
    uint32 field_sel_id = 0;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;
    uint32 cmd = 0;

    sal_memset(&mpls_key, 0, sizeof(mpls_key));
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_key->gport, lchip, src_lport);

    /* process gport, logic-port, metadata */
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_GPORT)
    {
        CTC_GLOBAL_PORT_CHECK(p_key->gport);
        src_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_key->gport);
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_LOGIC_PORT)
    {
        SYS_LOGIC_PORT_CHECK(p_key->logic_port);
        src_port = p_key->logic_port;
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        CTC_MAX_VALUE_CHECK(p_key->logic_port, 0x3FFF);
        src_port = p_key->logic_port;
    }

    if (p_key->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_src_port));
        field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_dest_port));
        field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
    }

    SetDsIpfixL3MplsHashKey(V, hashKeyType_f, &mpls_key, SYS_IPFIX_HASH_TYPE_MPLS);
    SetDsIpfixL3MplsHashKey(V, localPhyPort_f, &mpls_key, src_lport);
    SetDsIpfixL3MplsHashKey(V, flowFieldSel_f, &mpls_key, field_sel_id);
    SetDsIpfixL3MplsHashKey(V, isEgressKey_f, &mpls_key, ((p_key->dir==CTC_EGRESS)?1:0));
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        SetDsIpfixL3MplsHashKey(V, metadata_f, &mpls_key, src_port);
    }
    else
    {
        SetDsIpfixL3MplsHashKey(V, globalSrcPort_f, &mpls_key, src_port);
    }

    /*gen mpls key*/
    if (p_key->l3_info.mpls.label_num > CTC_IPFIX_LABEL_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }
    SetDsIpfixL3MplsHashKey(V, labelNum_f, &mpls_key, p_key->l3_info.mpls.label_num);
    SetDsIpfixL3MplsHashKey(V, mplsExp0_f, &mpls_key, p_key->l3_info.mpls.label[0].exp);
    SetDsIpfixL3MplsHashKey(V, mplsLabel0_f, &mpls_key, p_key->l3_info.mpls.label[0].label);
    SetDsIpfixL3MplsHashKey(V, mplsSbit0_f, &mpls_key, p_key->l3_info.mpls.label[0].sbit);
    SetDsIpfixL3MplsHashKey(V, mplsTtl0_f, &mpls_key, p_key->l3_info.mpls.label[0].ttl);
    SetDsIpfixL3MplsHashKey(V, mplsExp1_f, &mpls_key, p_key->l3_info.mpls.label[1].exp);
    SetDsIpfixL3MplsHashKey(V, mplsLabel1_f, &mpls_key, p_key->l3_info.mpls.label[1].label);
    SetDsIpfixL3MplsHashKey(V, mplsSbit1_f, &mpls_key, p_key->l3_info.mpls.label[1].sbit);
    SetDsIpfixL3MplsHashKey(V, mplsTtl1_f, &mpls_key, p_key->l3_info.mpls.label[1].ttl);
    SetDsIpfixL3MplsHashKey(V, mplsExp2_f, &mpls_key, p_key->l3_info.mpls.label[2].exp);
    SetDsIpfixL3MplsHashKey(V, mplsLabel2_f, &mpls_key, p_key->l3_info.mpls.label[2].label);
    SetDsIpfixL3MplsHashKey(V, mplsSbit2_f, &mpls_key, p_key->l3_info.mpls.label[2].sbit);
    SetDsIpfixL3MplsHashKey(V, mplsTtl2_f, &mpls_key, p_key->l3_info.mpls.label[2].ttl);

    sal_memcpy((uint8*)p_data, (uint8*)&mpls_key, sizeof(mpls_key));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_ipv6_hashkey(uint8 lchip, ctc_ipfix_data_t* p_key, void* p_data)
{
    DsIpfixL3Ipv6HashKey_m ipv6_key;
    IpfixL3Ipv6HashFieldSelect_m  hash_field;
    uint16 src_port = 0;
    uint16 src_lport = 0;
    ipv6_addr_t hw_ip6;
    uint16 l4_srcport = 0;
    uint32 field_sel_id = 0;
    DsSrcPort_m ds_src_port;
    DsDestPort_m ds_dest_port;
    uint32 cmd = 0;

    /* check l4 info para*/
    if (p_key->l4_info.type.l4_type > 0xF)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    if (p_key->l4_info.l4_sub_type >= CTC_IPFIX_L4_SUB_TYPE_MAX)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    /* this check is used for l4_type enum, API value transfer to chip value */
    if (p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_SCTP)
    {
        p_key->l4_info.type.l4_type =13;
    }

    sal_memset(&ipv6_key, 0, sizeof(ipv6_key));

    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_key->gport, lchip, src_lport);

    /* process gport, logic-port, metadata */
    if (p_key->port_type == CTC_IPFIX_PORT_TYPE_GPORT)
    {
        CTC_GLOBAL_PORT_CHECK(p_key->gport);
        src_port = SYS_MAP_CTC_GPORT_TO_DRV_GPORT(p_key->gport);
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_LOGIC_PORT)
    {
        SYS_LOGIC_PORT_CHECK(p_key->logic_port);
        src_port = p_key->logic_port;
    }
    else if(p_key->port_type == CTC_IPFIX_PORT_TYPE_METADATA)
    {
        CTC_MAX_VALUE_CHECK(p_key->logic_port, 0x3FFF);
        src_port = p_key->logic_port;
    }

    if (p_key->dir == CTC_INGRESS)
    {
        cmd = DRV_IOR( DsSrcPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_src_port));
        field_sel_id = GetDsSrcPort(V, ipfixHashFieldSel_f, &ds_src_port);
    }
    else
    {
        cmd = DRV_IOR( DsDestPort_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, src_lport, cmd, &ds_dest_port));
        field_sel_id = GetDsDestPort(V, portFlowHashFieldSel_f, &ds_dest_port);
    }

    SetDsIpfixL3Ipv6HashKey(V, hashKeyType0_f, &ipv6_key, SYS_IPFIX_HASH_TYPE_IPV6);
    SetDsIpfixL3Ipv6HashKey(V, hashKeyType1_f, &ipv6_key, SYS_IPFIX_HASH_TYPE_IPV6);
    SetDsIpfixL3Ipv6HashKey(V, localPhyPort_f, &ipv6_key, src_lport);
    SetDsIpfixL3Ipv6HashKey(V, flowFieldSel_f, &ipv6_key, field_sel_id);
    SetDsIpfixL3Ipv6HashKey(V, isEgressKey_f, &ipv6_key, ((p_key->dir==CTC_EGRESS)?1:0));
    SetDsIpfixL3Ipv6HashKey(V, globalSrcPort_f, &ipv6_key, src_port);
    SetDsIpfixL3Ipv6HashKey(V, dscp_f, &ipv6_key, p_key->l3_info.ipv6.dscp);

    cmd = DRV_IOR( IpfixL3Ipv6HashFieldSelect_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, field_sel_id, cmd, &hash_field));
    /*gen l3 key*/
    IPFIX_IPV6_MASK_LEN_CHECK(p_key->l3_info.ipv6.ipsa_masklen);
    IPFIX_IPV6_MASK_LEN_CHECK(p_key->l3_info.ipv6.ipda_masklen);

    IPFIX_IPV6_MASK(p_key->l3_info.ipv6.ipsa, p_key->l3_info.ipv6.ipsa_masklen);
    IPFIX_IPV6_MASK(p_key->l3_info.ipv6.ipda, p_key->l3_info.ipv6.ipda_masklen);

    IPFIX_SET_HW_IP6(hw_ip6, p_key->l3_info.ipv6.ipda);
    SetDsIpfixL3Ipv6HashKey(A, ipDa_f, &ipv6_key, hw_ip6);

    IPFIX_SET_HW_IP6(hw_ip6, p_key->l3_info.ipv6.ipsa);
    SetDsIpfixL3Ipv6HashKey(A, ipSa_f, &ipv6_key, hw_ip6);

    /*gen l4 key*/
    SetDsIpfixL3Ipv6HashKey(V, isMergeKey_f, &ipv6_key, p_key->l4_info.aware_tunnel_info_en);
    SetDsIpfixL3Ipv6HashKey(V, layer4Type_f, &ipv6_key, p_key->l4_info.type.l4_type);
    if (p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_ICMP)
    {
        l4_srcport = (p_key->l4_info.icmp.icmpcode) & 0xff;
        l4_srcport |=  (((p_key->l4_info.icmp.icmp_type) & 0xff)<<8);
        SetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4SourcePort_f, &ipv6_key, l4_srcport);
    }

    else if (p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_GRE)
    {
        SetDsIpfixL3Ipv6HashKey(V, uL4_gKey_greKey_f, &ipv6_key, p_key->l4_info.gre_key);
    }

    else if((p_key->l4_info.type.l4_type == CTC_IPFIX_L4_TYPE_UDP) && (p_key->l4_info.l4_sub_type == CTC_IPFIX_L4_SUB_TYPE_VXLAN))
    {
        SetDsIpfixL3Ipv6HashKey(V, isVxlan_f, &ipv6_key, 1);
        SetDsIpfixL3Ipv6HashKey(V, uL4_gVxlan_vni_f, &ipv6_key, p_key->l4_info.vni);
    }
    else
    {
        SetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4DestPort_f, &ipv6_key, p_key->l4_info.l4_port.dest_port);
        SetDsIpfixL3Ipv6HashKey(V, uL4_gPort_l4SourcePort_f, &ipv6_key, p_key->l4_info.l4_port.source_port);
    }

    /*l3 flow_label*/
    if (GetIpfixL3Ipv6HashFieldSelect(V, ipv6FlowLabelEn_f, &hash_field))
    {
        SetDsIpfixL3Ipv6HashKey(V, uL4_gKey_greKey_f, &ipv6_key, p_key->l3_info.ipv6.flow_label);
    }

    sal_memcpy((uint8*)p_data, (uint8*)&ipv6_key, sizeof(ipv6_key));

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_hash_key(uint8 lchip, ctc_ipfix_data_t* p_key, drv_ipfix_acc_in_t* p_in)
{

    switch(p_key->key_type)
    {
        case CTC_IPFIX_KEY_HASH_MAC:
            CTC_ERROR_RETURN(_sys_goldengate_ipfix_encode_l2_hashkey(lchip, p_key, p_in->data));
            p_in->type = 0; /*170bit*/
            p_in->tbl_id = DsIpfixL2HashKey_t;
            break;
        case CTC_IPFIX_KEY_HASH_IPV4:
            CTC_ERROR_RETURN(_sys_goldengate_ipfix_encode_ipv4_hashkey(lchip, p_key, p_in->data));
            p_in->type = 0; /*170bit*/
            p_in->tbl_id = DsIpfixL3Ipv4HashKey_t;
            break;
        case CTC_IPFIX_KEY_HASH_IPV6:
            CTC_ERROR_RETURN(_sys_goldengate_ipfix_encode_ipv6_hashkey(lchip, p_key, p_in->data));
            p_in->type = 1; /*340bit*/
            p_in->tbl_id = DsIpfixL3Ipv6HashKey_t;
            break;
        case CTC_IPFIX_KEY_HASH_MPLS:
            CTC_ERROR_RETURN(_sys_goldengate_ipfix_encode_mpls_hashkey(lchip, p_key, p_in->data));
            p_in->type = 0; /*170bit*/
            p_in->tbl_id = DsIpfixL3MplsHashKey_t;
            break;
        case CTC_IPFIX_KEY_HASH_L2_L3:
            CTC_ERROR_RETURN(_sys_goldengate_ipfix_encode_l2l3_hashkey(lchip, p_key, p_in->data));
            p_in->type = 1; /*340bit*/
            p_in->tbl_id = DsIpfixL2L3HashKey_t;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_goldengate_ipfix_encode_hash_ad(uint8 lchip, ctc_ipfix_data_t* p_key, drv_ipfix_acc_in_t* p_in)
{
    DsIpfixSessionRecord_m ad_rec;
    uint32 dest_type = SYS_IPFIX_UNICAST_DEST;
    uint32 lport = 0;
    uint32 byte_cnt[2] = {0};

    sal_memset(&ad_rec, 0, sizeof(ad_rec));
    SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(p_key->dest_gport, lchip, lport);

    if (p_key->flags & CTC_IPFIX_DATA_L2_MCAST_DETECTED)
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gL2Mcast_l2McGroupId_f , &ad_rec, p_key->dest_group_id);
        dest_type = SYS_IPFIX_L2MC_DEST;
    }
    if (p_key->flags & CTC_IPFIX_DATA_L3_MCAST_DETECTED)
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gL3Mcast_l3McGroupId_f , &ad_rec, p_key->dest_group_id);
        dest_type = SYS_IPFIX_L3MC_DEST;
    }
    if (p_key->flags & CTC_IPFIX_DATA_BCAST_DETECTED)
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gBcast_floodingId_f, &ad_rec, p_key->dest_group_id);
        dest_type = SYS_IPFIX_BCAST_DEST;
    }
    if (p_key->flags & CTC_IPFIX_DATA_APS_DETECTED)
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gApsGroup_apsGroupId_f, &ad_rec, p_key->dest_group_id);
        dest_type = SYS_IPFIX_APS_DEST;
    }
    if (p_key->flags & CTC_IPFIX_DATA_ECMP_DETECTED)
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gEcmpGroup_ecmpGroupId_f, &ad_rec, p_key->dest_group_id);
        dest_type = SYS_IPFIX_ECMP_DEST;
    }
    if (p_key->flags & CTC_IPFIX_DATA_LINKAGG_DETECTED)
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToLagg_linkaggGroupId_f, &ad_rec, p_key->dest_group_id);
        dest_type = SYS_IPFIX_LINKAGG_DEST;
    }
    else
    {
        SetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToNormal_destChipId_f, &ad_rec, lchip);
        SetDsIpfixSessionRecord(V, uDestinationInfo_gUcastToNormal_destId_f, &ad_rec, lport);
    }

    byte_cnt[0] = (uint32)p_key->byte_count;
    byte_cnt[1] = (uint32)(p_key->byte_count >> 32);


    SetDsIpfixSessionRecord(V, exportReason_f, &ad_rec, p_key->export_reason);
    SetDsIpfixSessionRecord(V, expired_f, &ad_rec, (p_key->export_reason==CTC_IPFIX_REASON_EXPIRED)?1:0);
    SetDsIpfixSessionRecord(V, droppedPacket_f , &ad_rec, (p_key->flags&CTC_IPFIX_DATA_DROP_DETECTED));
    SetDsIpfixSessionRecord(V, destinationType_f, &ad_rec, dest_type);
    SetDsIpfixSessionRecord(A, byteCount_f, &ad_rec, byte_cnt);
    SetDsIpfixSessionRecord(V, packetCount_f, &ad_rec, p_key->pkt_count);
     /*-SetDsIpfixSessionRecord(V, firstPktRecvDone_f, &ad_rec, p_key->pkt_count);*/
    SetDsIpfixSessionRecord(V, firstTs_f, &ad_rec, p_key->start_timestamp);
    SetDsIpfixSessionRecord(V, fragment_f , &ad_rec, (p_key->flags&CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED)?1:0);
    SetDsIpfixSessionRecord(V, lastTs_f, &ad_rec, p_key->last_timestamp);
    SetDsIpfixSessionRecord(V, minTtl_f, &ad_rec, p_key->min_ttl);
    SetDsIpfixSessionRecord(V, maxTtl_f, &ad_rec, p_key->max_ttl);
    SetDsIpfixSessionRecord(V, nonFragment_f, &ad_rec, (p_key->flags&CTC_IPFIX_DATA_FRAGMENT_PKT_DETECTED)?0:1);
    SetDsIpfixSessionRecord(V, ttlChanged_f, &ad_rec, (p_key->flags&CTC_IPFIX_DATA_TTL_CHANGE)?1:0);

    sal_memcpy((uint8*)p_in->data, (uint8*)&ad_rec, sizeof(ad_rec));
    return CTC_E_NONE;
}

/**
   @brief only used for lookup key by key
*/
int32
_sys_goldengate_ipfix_get_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key, uint32* rst_hit, uint32* rst_key_index)
{
    int32 ret = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;

    if (p_key->key_type >= CTC_IPFIX_KEY_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.data = mem_malloc(MEM_IPFIX_MODULE, 16*4);
    if (in.data == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    ret = _sys_goldengate_ipfix_encode_hash_key(lchip, p_key, &in);
    if (ret < 0)
    {
        mem_free(in.data);
        return ret;
    }

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_LKP_BY_KEY, &in, &out);
    if (ret < 0)
    {
        mem_free(in.data);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        return ret;
    }

    (*rst_key_index) = out.key_index;

    if ((out.hit) && !(out.invalid_idx))
    {
        (*rst_hit) = 1;
    }
    else
    {
        (*rst_hit) = 0;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

    mem_free(in.data);

    if (0 == (*rst_hit))
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    return CTC_E_NONE;
}

/**
   @brief only used for lookup key by key
*/
int32
sys_goldengate_ipfix_get_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key, uint32* p_rst_hit, uint32* p_rst_key_index)
{
    int32 ret = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_key);
    SYS_IPFIX_CHECK_DIR(p_key->dir);
    SYS_MAP_GPORT_TO_LCHIP(p_key->gport, lchip);

    if (p_key->key_type >= CTC_IPFIX_KEY_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    ret = _sys_goldengate_ipfix_get_entry_by_key(lchip, p_key, p_rst_hit, p_rst_key_index);
    if (ret)
    {
        return ret;
    }

    return CTC_E_NONE;
}

/**
   @brief  used for lookup key by index
*/
int32
sys_goldengate_ipfix_get_entry_by_index(uint8 lchip, uint32 index, uint8 key_type, ctc_ipfix_data_t* p_out)
{
    int32 ret = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;
    int8 hashKeyType =0;
    uint8 is_340bits = 0;
    drv_work_platform_type_t platform_type;
    DsIpfixL2HashKey_m* l2_key =NULL;
    uint32 ipfix_max_entry_num = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    p_out->key_type = key_type;
    CTC_PTR_VALID_CHECK(p_out);
    if (key_type >= CTC_IPFIX_KEY_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num));
    if (index > (ipfix_max_entry_num-1))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "type:%d, index:%d\n", p_out->key_type, index);

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    drv_goldengate_get_platform_type(&platform_type);

    if ((key_type == CTC_IPFIX_KEY_HASH_IPV6) || (key_type == CTC_IPFIX_KEY_HASH_L2_L3))
    {
        is_340bits = 1;
    }

    in.tbl_id = (key_type==CTC_IPFIX_KEY_HASH_MAC)?DsIpfixL2HashKey_t:
            ((key_type==CTC_IPFIX_KEY_HASH_IPV4)?DsIpfixL3Ipv4HashKey_t:
            ((key_type==CTC_IPFIX_KEY_HASH_MPLS)?DsIpfixL3MplsHashKey_t:
            ((key_type==CTC_IPFIX_KEY_HASH_IPV6)?DsIpfixL3Ipv6HashKey_t:DsIpfixL2L3HashKey_t)));

    /* for RTL bug 7131*/
    if (platform_type == HW_PLATFORM)
    {
        if (!is_340bits)
        {
            index = ((index & 0xfffffffd) | (~index & 0x02));
        }
    }

    in.index = index;
    in.type = is_340bits;

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "tbl_id:%d\n",in.tbl_id);

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_LKP_KEY_BY_IDX, &in, &out);
    if (ret < 0)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        return ret;
    }

    l2_key = (DsIpfixL2HashKey_m*)(out.data);
    hashKeyType = GetDsIpfixL2HashKey(V, hashKeyType_f, l2_key);

    if ((key_type+1) != hashKeyType)
    {
        return CTC_E_UNEXPECT;
    }

    if (hashKeyType == 0)
    {
        return CTC_E_ENTRY_NOT_EXIST;
    }

    if (out.invalid_idx)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_hashkey_ad(lchip, p_out, 0, (void*)out.data));

    return CTC_E_NONE;
}


/**
   @brief  used for lookup ad by index
*/
int32
sys_goldengate_ipfix_get_ad_by_index(uint8 lchip, uint32 index, ctc_ipfix_data_t* p_out)
{
    int32 ret = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;
    uint32 ipfix_max_entry_num = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "index:%d\n", index);

    CTC_PTR_VALID_CHECK(p_out);
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num));
    if (index > (ipfix_max_entry_num-1))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.tbl_id = DsIpfixSessionRecord_t;
    in.index = index;
    in.type = 0;

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_LKP_AD_BY_IDX, &in, &out);
    if (ret < 0)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        return ret;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

    if (out.invalid_idx)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(_sys_goldengate_ipfix_decode_hashkey_ad(lchip, p_out, 1, (void*)out.data));

    return CTC_E_NONE;
}

STATIC uint8
_sys_goldengate_ipfix_entry_is_local_check(uint8 lchip, ctc_ipfix_data_t* p_key)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_id = 0;
    ds_t   hash_field = {0};
    uint8  mapped_lchip = 0;

    switch (p_key->key_type)
    {
        case CTC_IPFIX_KEY_HASH_MAC:
            tbl_id = IpfixL2HashFieldSelect_t;
            field_id = IpfixL2HashFieldSelect_globalSrcPortEn_f;
            break;
        case CTC_IPFIX_KEY_HASH_L2_L3:
            tbl_id = IpfixL2L3HashFieldSelect_t;
            field_id = IpfixL2L3HashFieldSelect_globalSrcPortEn_f;
            break;
        case CTC_IPFIX_KEY_HASH_IPV4:
            tbl_id = IpfixL3Ipv4HashFieldSelect_t;
            field_id = IpfixL3Ipv4HashFieldSelect_globalSrcPortEn_f;
            break;
        case CTC_IPFIX_KEY_HASH_IPV6:
            tbl_id = IpfixL3Ipv6HashFieldSelect_t;
            field_id = IpfixL3Ipv6HashFieldSelect_globalSrcPortEn_f;
            break;
        case CTC_IPFIX_KEY_HASH_MPLS:
            tbl_id = IpfixL3MplsHashFieldSelect_t;
            field_id = IpfixL3MplsHashFieldSelect_globalSrcPortEn_f;
            break;

    }

    cmd = DRV_IOR(tbl_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, p_key->field_sel_id, cmd, &hash_field));

    if(DRV_GET_FIELD_V(tbl_id, field_id, &hash_field))
    {
        if(sys_goldengate_get_local_chip_id(SYS_MAP_CTC_GPORT_TO_GCHIP(p_key->gport), &mapped_lchip) ||
            lchip != mapped_lchip)
        {
            return 0;
        }
    }

    return 1;
}

/**
   @brief only used for write key by key
*/
int32
sys_goldengate_ipfix_add_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key)
{
    int32 ret = 0;
    uint8 step = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;

    SYS_IPFIX_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_key);
    SYS_IPFIX_CHECK_DIR(p_key->dir);

    SYS_IPFIX_LOCK(lchip);

    /* entry is not local, do nothing*/
    if(!_sys_goldengate_ipfix_entry_is_local_check(lchip, p_key))
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    if (p_key->key_type >= CTC_IPFIX_KEY_NUM)
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    if ((p_key->key_type == CTC_IPFIX_KEY_HASH_IPV6) || (p_key->key_type == CTC_IPFIX_KEY_HASH_L2_L3))
    {
        step = 2;
    }
    else
    {
        step = 1;
    }

    if (sys_goldengate_ipfix_get_flow_cnt(lchip, 0) + step > SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPFIX))
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_NO_RESOURCE;
    }

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.data = mem_malloc(MEM_IPFIX_MODULE, 16*4);
    if (in.data == NULL)
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    ret = _sys_goldengate_ipfix_encode_hash_key(lchip, p_key, &in);
    if (ret < 0)
    {
        mem_free(in.data);
        SYS_IPFIX_UNLOCK(lchip);
        return ret;
    }

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_LKP_BY_KEY, &in, &out);
    if (ret < 0)
    {
        mem_free(in.data);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        SYS_IPFIX_UNLOCK(lchip);
        return ret;
    }

    if ((out.hit) && !(out.invalid_idx))
    {
        mem_free(in.data);
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_ENTRY_EXIST;
    }

    if (out.conflict)
    {
        mem_free(in.data);
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_HASH_CONFLICT;
    }

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_WRITE_BY_KEY, &in, &out);
    if (ret < 0)
    {
        mem_free(in.data);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        SYS_IPFIX_UNLOCK(lchip);
        return ret;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

    if (out.invalid_idx)
    {
        mem_free(in.data);
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }

    mem_free(in.data);
    SYS_IPFIX_UNLOCK(lchip);

    return CTC_E_NONE;
}

/**
   @brief only used for write key by key
*/
int32
sys_goldengate_ipfix_add_entry_by_index(uint8 lchip, ctc_ipfix_data_t* p_key, uint32 index)
{
    int32 ret = 0;
    uint8 step = 0;
    int8 hashKeyType =0;
    uint32 is_340bits = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;
    DsIpfixL2HashKey_m* l2_key =NULL;
    uint32 ipfix_max_entry_num = 0;

    CTC_PTR_VALID_CHECK(p_key);
    SYS_IPFIX_CHECK_DIR(p_key->dir);

    if (p_key->key_type >= CTC_IPFIX_KEY_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((p_key->key_type == CTC_IPFIX_KEY_HASH_IPV6) || (p_key->key_type == CTC_IPFIX_KEY_HASH_L2_L3))
    {
        is_340bits = 1;
        step = 2;
    }
    else
    {
        step = 1;
    }

    if (sys_goldengate_ipfix_get_flow_cnt(lchip, 0) + step > SYS_FTM_SPEC(lchip, CTC_FTM_SPEC_IPFIX))
    {
        return CTC_E_NO_RESOURCE;
    }

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num));
    if (index > (ipfix_max_entry_num-1))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%x, type:0x%x \n", index, p_key->key_type);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "type:%d,index:%d\n", p_key->key_type, index);

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.data = mem_malloc(MEM_IPFIX_MODULE, 16*4);
    if (in.data == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    in.tbl_id = (p_key->key_type==CTC_IPFIX_KEY_HASH_MAC)?DsIpfixL2HashKey_t:
            ((p_key->key_type==CTC_IPFIX_KEY_HASH_IPV4)?DsIpfixL3Ipv4HashKey_t:
            ((p_key->key_type==CTC_IPFIX_KEY_HASH_MPLS)?DsIpfixL3MplsHashKey_t:
            ((p_key->key_type==CTC_IPFIX_KEY_HASH_IPV6)?DsIpfixL3Ipv6HashKey_t:DsIpfixL2L3HashKey_t)));

    in.index = index;
    in.type = is_340bits;

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_LKP_KEY_BY_IDX, &in, &out);
    if (ret < 0)
    {
        mem_free(in.data);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        return ret;
    }

    l2_key = (DsIpfixL2HashKey_m*)(out.data);
    hashKeyType = GetDsIpfixL2HashKey(V, hashKeyType_f, l2_key);
    if (hashKeyType != 0)
    {
        mem_free(in.data);
        return CTC_E_ENTRY_EXIST;
    }

    ret = _sys_goldengate_ipfix_encode_hash_key(lchip, p_key, &in);
    if (ret < 0)
    {
        mem_free(in.data);
        return ret;
    }

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_WRITE_KEY_BY_IDX, &in, &out);
    if (ret < 0)
    {
        mem_free(in.data);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        return ret;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

   if (out.invalid_idx)
    {
        mem_free(in.data);
        return CTC_E_INVALID_PARAM;
    }

    mem_free(in.data);

    return CTC_E_NONE;
}

/**
   @brief only used for write key by key
*/
int32
sys_goldengate_ipfix_add_ad_by_index(uint8 lchip, ctc_ipfix_data_t* p_key, uint32 index)
{
    int32 ret = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;
    drv_work_platform_type_t platform_type;
    uint32 ipfix_max_entry_num = 0;

    CTC_PTR_VALID_CHECK(p_key);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%x, flag:0x%x, reason:%d, dest_id:%d, dest_grp:%d \n", index, p_key->flags, p_key->export_reason,
        p_key->dest_gport, p_key->dest_group_id);

    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num));
    if (index > (ipfix_max_entry_num-1))
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.data = mem_malloc(MEM_IPFIX_MODULE, 16*4);
    if (in.data == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    drv_goldengate_get_platform_type(&platform_type);

    /* for RTL bug 7109*/
#if 0
    if (platform_type == HW_PLATFORM)
    {
        index = ((index & 0xfffffffd) | (~index & 0x02));
    }
#endif

    in.index = index;
    in.tbl_id = DsIpfixSessionRecord_t;
    ret = _sys_goldengate_ipfix_encode_hash_ad(lchip, p_key, &in);
    if (ret < 0)
    {
        mem_free(in.data);
        return ret;
    }

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_WRITE_AD_BY_IDX, &in, &out);
    if (ret < 0)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        mem_free(in.data);
        return ret;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

    if (out.invalid_idx)
    {
        mem_free(in.data);
        return CTC_E_INVALID_PARAM;
    }

    mem_free(in.data);

    return CTC_E_NONE;
}


/**
   @brief only used for delete ad by index
*/
int32
sys_goldengate_ipfix_delete_ad_by_index(uint8 lchip, uint32 index)
{
    int32 ret = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;
    uint32 ipfix_max_entry_num = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%x\n", index);

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.data = mem_malloc(MEM_IPFIX_MODULE, 16*4);
    if (in.data == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    ret = sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num);
    if (ret < 0)
    {
        mem_free(in.data);
        return ret;
    }

    if (index > (ipfix_max_entry_num-1))
    {
        mem_free(in.data);
        return CTC_E_EXCEED_MAX_SIZE;
    }

    in.tbl_id = DsIpfixSessionRecord_t;
    in.index = index;
    in.type = 0;

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_DEL_AD_BY_IDX, &in, &out);
    if (ret < 0)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        mem_free(in.data);
        return ret;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

    if (out.invalid_idx)
    {
        mem_free(in.data);
        return CTC_E_INVALID_PARAM;
    }

    mem_free(in.data);

    return CTC_E_NONE;
}


/**
   @brief only used for delete ad by index
*/
int32
sys_goldengate_ipfix_delete_entry_by_index(uint8 lchip, uint8 type, uint32 index)
{
    int32 ret = 0;
    drv_ipfix_acc_in_t in;
    drv_ipfix_acc_out_t out;
    uint32 is_340bits = 0;
    ctc_ipfix_data_t p_out;
    uint32 ipfix_max_entry_num = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "index:0x%x, type:%d\n", index, type);

    sal_memset(&in, 0, sizeof(in));
    sal_memset(&out, 0, sizeof(out));

    in.data = mem_malloc(MEM_IPFIX_MODULE, 16*4);
    if (in.data == NULL)
    {
        return CTC_E_NO_MEMORY;
    }

    ret = sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num);
    if (ret < 0)
    {
        mem_free(in.data);
        return ret;
    }

    if (index > (ipfix_max_entry_num-1))
    {
        mem_free(in.data);
        return CTC_E_EXCEED_MAX_SIZE;
    }

     /*xupc*/
    ret = sys_goldengate_ipfix_get_entry_by_index(lchip, index, type, &p_out);
    if(ret < 0)
    {
        mem_free(in.data);
        return ret;
    }

    if ((type == CTC_IPFIX_KEY_HASH_IPV6) || (type == CTC_IPFIX_KEY_HASH_L2_L3))
    {
        is_340bits = 1;
    }

    in.tbl_id = (type==CTC_IPFIX_KEY_HASH_MAC)?DsIpfixL2HashKey_t:
            ((type==CTC_IPFIX_KEY_HASH_IPV4)?DsIpfixL3Ipv4HashKey_t:
            ((type==CTC_IPFIX_KEY_HASH_MPLS)?DsIpfixL3MplsHashKey_t:
            ((type==CTC_IPFIX_KEY_HASH_IPV6)?DsIpfixL3Ipv6HashKey_t:DsIpfixL2L3HashKey_t)));

    in.index = index;
    in.type = is_340bits;
    in.index = index;

    ret = drv_goldengate_ipfix_acc(lchip, DRV_IPFIX_ACC_DEL_KEY_BY_IDX, &in, &out);
    if (ret < 0)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ipfix acc failed! \n");
        mem_free(in.data);
        return ret;
    }

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Ipfix acc out, key_index:0x%x, conflict:%d, hit:%d, free:%d, invalid:%d\n",
        out.key_index, out.conflict, out.hit, out.free, out.invalid_idx);

    mem_free(in.data);

    return CTC_E_NONE;
}


/**
   @brief only used for delete key by key
*/
int32
sys_goldengate_ipfix_delete_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key)
{
    uint32 type = 0;
    uint32 rst_hit = 0;
    uint32 rst_key_index = 0;
   /*  ctc_ipfix_global_cfg_t global_cfg;*/

    SYS_IPFIX_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_key);
    SYS_IPFIX_CHECK_DIR(p_key->dir);
    IPFIX_IPV4_MASK(p_key->l3_info.ipv4.ipsa, p_key->l3_info.ipv4.ipsa_masklen);
    IPFIX_IPV4_MASK(p_key->l3_info.ipv4.ipda, p_key->l3_info.ipv4.ipda_masklen);
    IPFIX_IPV6_MASK(p_key->l3_info.ipv6.ipsa, p_key->l3_info.ipv6.ipsa_masklen);
    IPFIX_IPV6_MASK(p_key->l3_info.ipv6.ipda, p_key->l3_info.ipv6.ipda_masklen);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "type:%d\n", type);

    SYS_IPFIX_LOCK(lchip);

    /* entry is not local, do nothing*/
    if(!_sys_goldengate_ipfix_entry_is_local_check(lchip, p_key))
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_NONE;
    }

    type = p_key->key_type;

    if (type >= CTC_IPFIX_KEY_NUM)
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_INVALID_PARAM;
    }
#if 0
    sal_memset(&global_cfg, 0, sizeof(ctc_ipfix_global_cfg_t));
    sys_goldengate_ipfix_get_global_cfg(lchip, &global_cfg);
    if (global_cfg.sw_learning == 0)
    {
        return CTC_E_INVALID_PARAM;
    }
#endif
    /* get entry by key */
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_goldengate_ipfix_get_entry_by_key(lchip, p_key, &rst_hit, &rst_key_index), p_gg_ipfix_master[lchip]->mutex);

    if (rst_hit)
    {
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_ipfix_delete_entry_by_index(lchip, type, rst_key_index), p_gg_ipfix_master[lchip]->mutex);
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_ipfix_delete_ad_by_index(lchip, rst_key_index), p_gg_ipfix_master[lchip]->mutex);
    }
    else
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_ENTRY_NOT_EXIST;
    }

    SYS_IPFIX_UNLOCK(lchip);
    return CTC_E_NONE;
}

/**
   @brief ipfix traverse interface
*/
int32
sys_goldengate_ipfix_traverse(uint8 lchip, ctc_ipfix_traverse_fn fn, ctc_ipfix_traverse_t* p_data, uint8 is_remove)
{
    int32 ret = 0;
    uint32 index = 0;
    uint32 count = 0;
    sys_dma_tbl_rw_t dma_rw;
    uint32 cfg_addr = 0;
    uint32 cfg_ad_addr =0;
    uint32* p_buffer = NULL;
    uint32* p_ad_buffer =NULL;
    void* p_key = NULL;
    void* p_ad = NULL;
    void* p_sub_key = NULL;
    void* p_sub_ad = NULL;
    uint8 sub_idx = 0;
    uint32 hash_type = 0;
    uint32 hash_type1 = 0;
    ctc_ipfix_data_t ipfix_data;
    uint32 base_entry = 0;
    uint32 entry_cnt = 0;
    uint32 cmd = 0;
    DsIpfixL2HashKey_m l2_key;
    DsIpfixSessionRecord_m l2_ad;
    uint32 next_index = 0;
    uint32 entry_index = 0;
    uint16 traverse_action = 0;
    drv_work_platform_type_t platform_type;
    uint32 ipfix_max_entry_num = 0;

    CTC_PTR_VALID_CHECK(fn);
    CTC_PTR_VALID_CHECK(p_data);
    SYS_IPFIX_INIT_CHECK(lchip);

    if(p_data->user_data != NULL)
    {
        traverse_action = *(uint16*)p_data->user_data;
    }

    /* get 85bits bits entry num */
    CTC_ERROR_RETURN(sys_goldengate_ftm_query_table_entry_num(lchip, DsIpfixL2HashKey_t, &ipfix_max_entry_num));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "ipfix_max_entry_num = %d\n", ipfix_max_entry_num);
    if (p_data->entry_num > ipfix_max_entry_num)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    sal_memset(&ipfix_data, 0, sizeof(ctc_ipfix_data_t));
    sal_memset(&dma_rw, 0, sizeof(ctc_dma_tbl_rw_t));

    SYS_IPFIX_LOCK(lchip);

    if (p_data->start_index%4)
    {
        cmd = DRV_IOR(DsIpfixL2HashKey_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, p_data->start_index, cmd, &l2_key), p_gg_ipfix_master[lchip]->mutex);
        hash_type = GetDsIpfixL3Ipv6HashKey(V, hashKeyType0_f, &l2_key);

        cmd = DRV_IOR(DsIpfixSessionRecord_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, p_data->start_index/2, cmd, &l2_ad), p_gg_ipfix_master[lchip]->mutex);

        if (hash_type == SYS_IPFIX_HASH_TYPE_L2)
        {
            ipfix_data.key_type = CTC_IPFIX_KEY_HASH_MAC;
            _sys_goldengate_ipfix_decode_l2_hashkey(lchip, &ipfix_data, &l2_key);
            _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, &l2_ad);

            if(is_remove)
            {
                ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                if(ret == CTC_E_NONE)
                {
                    CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, p_data->start_index), p_gg_ipfix_master[lchip]->mutex);
                }
            }
            else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
            {
                entry_index = p_data->start_index;
                fn(&ipfix_data, &entry_index);
            }
            else
            {
                fn(&ipfix_data, p_data->user_data);
            }

            count++;
        }
        else if (hash_type == SYS_IPFIX_HASH_TYPE_IPV4)
        {
            ipfix_data.key_type = CTC_IPFIX_KEY_HASH_IPV4;
            cmd = DRV_IOR(DsIpfixL3Ipv4HashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, p_data->start_index, cmd, &l2_key), p_gg_ipfix_master[lchip]->mutex);
            _sys_goldengate_ipfix_decode_ipv4_hashkey(lchip, &ipfix_data, (DsIpfixL3Ipv4HashKey_m*)&l2_key);
            _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, &l2_ad);


            if(is_remove)
            {
                ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                if(ret == CTC_E_NONE)
                {
                    CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, p_data->start_index), p_gg_ipfix_master[lchip]->mutex);
                }
            }
            else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
            {
                entry_index = p_data->start_index;
                fn(&ipfix_data, &entry_index);
            }
            else
            {
                fn(&ipfix_data, p_data->user_data);
            }

            count++;
        }
        else if (hash_type == SYS_IPFIX_HASH_TYPE_MPLS)
        {
            ipfix_data.key_type = CTC_IPFIX_KEY_HASH_MPLS;
            cmd = DRV_IOR(DsIpfixL3MplsHashKey_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, p_data->start_index, cmd, &l2_key), p_gg_ipfix_master[lchip]->mutex);
            _sys_goldengate_ipfix_decode_mpls_hashkey(lchip, &ipfix_data, (DsIpfixL3MplsHashKey_m*)&l2_key);
            _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, &l2_ad);

            if(is_remove)
            {
                ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                if(ret == CTC_E_NONE)
                {
                    CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, p_data->start_index), p_gg_ipfix_master[lchip]->mutex);
                }
            }
            else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
            {
                entry_index = p_data->start_index;
                fn(&ipfix_data, &entry_index);
            }
            else
            {
                fn(&ipfix_data, p_data->user_data);
            }
            count++;
        }
    }


    p_buffer = (uint32*)mem_malloc(MEM_IPFIX_MODULE, 100*DS_IPFIX_L3_IPV6_HASH_KEY_BYTES);
    if (NULL == p_buffer)
    {
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    p_ad_buffer = (uint32*)mem_malloc(MEM_IPFIX_MODULE, 200*DS_IPFIX_SESSION_RECORD_BYTES);
    if (NULL == p_ad_buffer)
    {
        mem_free(p_buffer);
        SYS_IPFIX_UNLOCK(lchip);
        return CTC_E_NO_MEMORY;
    }

    base_entry = p_data->start_index + p_data->start_index%4;

    do
    {
        if (base_entry >= ipfix_max_entry_num)
        {
            p_data->is_end = 1;
            mem_free(p_buffer);
            mem_free(p_ad_buffer);
            SYS_IPFIX_UNLOCK(lchip);
            return CTC_E_NONE;
        }

        /*Notice: start_index and next_traverse_index is 85bits encoding index, for dma performance, every
        Dma operation is by 340bits */
         /*-sal_memset(p_buffer, 0, 100*DS_IPFIX_L3_IPV6_HASH_KEY_BYTES);*/
         /*-sal_memset(p_ad_buffer, 0, 200*DS_IPFIX_SESSION_RECORD_BYTES);*/

        entry_cnt = ((base_entry+100*4) <= ipfix_max_entry_num)?100:(ipfix_max_entry_num-base_entry)/4;
        drv_goldengate_get_platform_type(&platform_type);
        if (platform_type == HW_PLATFORM)
        {
            CTC_ERROR_GOTO(drv_goldengate_table_get_hw_addr(DsIpfixL3Ipv6HashKey_t, base_entry, &cfg_addr, FALSE), ret, error);
            CTC_ERROR_GOTO(drv_goldengate_table_get_hw_addr(DsIpfixSessionRecord_t, base_entry/2, &cfg_ad_addr, FALSE), ret, error);
        }
        else
        {
            /*special process for uml*/
            cfg_addr = (DsIpfixL3Ipv6HashKey_t << 18)|base_entry;

            cfg_ad_addr = (DsIpfixSessionRecord_t << 18)|base_entry/2;
        }


        /*Using Dma to read data, using 340bits entry read*/
        dma_rw.rflag = 1;
        dma_rw.tbl_addr = cfg_addr;
        dma_rw.entry_num = entry_cnt;
        dma_rw.entry_len = DS_IPFIX_L3_IPV6_HASH_KEY_BYTES;
        dma_rw.buffer = p_buffer;

        CTC_ERROR_GOTO(sys_goldengate_dma_rw_table(lchip, &dma_rw), ret, error);

        dma_rw.rflag = 1;
        dma_rw.tbl_addr = cfg_ad_addr;
        dma_rw.entry_num = entry_cnt*2;
        dma_rw.entry_len = DS_IPFIX_SESSION_RECORD_BYTES;
        dma_rw.buffer = p_ad_buffer;

        CTC_ERROR_GOTO(sys_goldengate_dma_rw_table(lchip, &dma_rw), ret, error);

        for (index = 0; index < entry_cnt; index++)
        {
            p_key = ((DsIpfixL3Ipv6HashKey_m*)p_buffer + index);
            p_ad = ((DsIpfixSessionRecord_m*)p_ad_buffer + 2*index);

            hash_type = GetDsIpfixL3Ipv6HashKey(V, hashKeyType0_f, p_key);
            hash_type1 = GetDsIpfixL3Ipv6HashKey(V, hashKeyType1_f, p_key);
            if ((hash_type == SYS_IPFIX_HASH_TYPE_INVALID)&&(hash_type1 == SYS_IPFIX_HASH_TYPE_INVALID))
            {
                if (index*4+base_entry > ipfix_max_entry_num-4)
                {
                    p_data->is_end = 1;
                    goto end_pro;
                }
                else
                {
                    continue;
                }
            }

             /*need memset for bug30410*/
            sal_memset(&ipfix_data, 0, sizeof(ctc_ipfix_data_t));

            if ((hash_type == SYS_IPFIX_HASH_TYPE_L2) || (hash_type1 == SYS_IPFIX_HASH_TYPE_L2))
            {
                ipfix_data.key_type = CTC_IPFIX_KEY_HASH_MAC;
                for (sub_idx = 0; sub_idx < 2; sub_idx++)
                {
                    p_sub_key = ((DsIpfixL2HashKey_m*)p_key + sub_idx);
                    p_sub_ad = ((DsIpfixSessionRecord_m*)p_ad + sub_idx);
                    if (GetDsIpfixL2HashKey(V, hashKeyType_f, p_sub_key) != SYS_IPFIX_HASH_TYPE_INVALID)
                    {
                        _sys_goldengate_ipfix_decode_l2_hashkey(lchip, &ipfix_data, (DsIpfixL2HashKey_m*)p_sub_key);
                        _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, (DsIpfixSessionRecord_m*)p_sub_ad);
                        if(is_remove)
                        {
                            ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                            if(ret == CTC_E_NONE)
                            {
                                CTC_ERROR_GOTO(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, index*4+base_entry+sub_idx*2), ret, error);
                            }
                        }
                        else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
                        {
                            entry_index = p_data->start_index;
                            fn(&ipfix_data, &entry_index);
                        }
                        else
                        {
                            fn(&ipfix_data, p_data->user_data);
                        }

                        count++;
                    }

                    if (base_entry+index*4+2*(sub_idx +1) > ipfix_max_entry_num-2)
                    {
                        p_data->is_end = 1;
                        goto end_pro;
                    }
                    else if (count >= p_data->entry_num)
                    {
                        next_index = (base_entry+4*index) + 2*(sub_idx +1);
                        goto end_pro;
                    }
                }
            }
            else if (hash_type == SYS_IPFIX_HASH_TYPE_L2L3)
            {
                p_sub_key = (DsIpfixL2L3HashKey_m*)p_key;
                p_sub_ad = (DsIpfixSessionRecord_m*)p_ad;
                ipfix_data.key_type = CTC_IPFIX_KEY_HASH_L2_L3;
                _sys_goldengate_ipfix_decode_l2l3_hashkey(lchip, &ipfix_data, (DsIpfixL2L3HashKey_m*)p_sub_key);
                _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, (DsIpfixSessionRecord_m*)p_sub_ad);
                if(is_remove)
                {
                    ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                    if(ret == CTC_E_NONE)
                    {
                        CTC_ERROR_GOTO(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, index*4+base_entry), ret, error);
                    }
                }
                else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
                {
                    entry_index = p_data->start_index;
                    fn(&ipfix_data, &entry_index);
                }
                else
                {
                    fn(&ipfix_data, p_data->user_data);
                }

                count += 2;

                if (index*4+base_entry > ipfix_max_entry_num-4)
                {
                    p_data->is_end = 1;
                    goto end_pro;
                }
                else if (count >= p_data->entry_num)
                {
                    next_index = (base_entry+4*(index+1));
                    goto end_pro;
                }
            }
            else if ((hash_type == SYS_IPFIX_HASH_TYPE_IPV4) ||(hash_type1 == SYS_IPFIX_HASH_TYPE_IPV4))
            {
                ipfix_data.key_type = CTC_IPFIX_KEY_HASH_IPV4;
                for (sub_idx = 0; sub_idx < 2; sub_idx++)
                {
                    p_sub_key = ((DsIpfixL3Ipv4HashKey_m*)p_key + sub_idx);
                    p_sub_ad = ((DsIpfixSessionRecord_m*)p_ad + sub_idx);

                    if (GetDsIpfixL3Ipv4HashKey(V, hashKeyType_f, p_sub_key) != SYS_IPFIX_HASH_TYPE_INVALID)
                    {
                        _sys_goldengate_ipfix_decode_ipv4_hashkey(lchip, &ipfix_data, (DsIpfixL3Ipv4HashKey_m*)p_sub_key);
                        _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, (DsIpfixSessionRecord_m*)p_sub_ad);
                        if(is_remove)
                        {
                            ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                            if(ret == CTC_E_NONE)
                            {
                                CTC_ERROR_GOTO(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, index*4+base_entry+sub_idx*2), ret, error);
                            }
                        }
                        else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
                        {
                            entry_index = p_data->start_index;
                            fn(&ipfix_data, &entry_index);
                        }
                        else
                        {
                            fn(&ipfix_data, p_data->user_data);
                        }
                        count++;
                    }

                    if (base_entry+index*4+2*(sub_idx +1) > ipfix_max_entry_num-2)
                    {
                        p_data->is_end = 1;
                        goto end_pro;
                    }
                    else if (count >= p_data->entry_num)
                    {
                        next_index = (base_entry+4*index) + 2*(sub_idx +1);
                        goto end_pro;
                    }
                }
            }
            else if (hash_type == SYS_IPFIX_HASH_TYPE_IPV6)
            {
                ipfix_data.key_type =  CTC_IPFIX_KEY_HASH_IPV6;
                _sys_goldengate_ipfix_decode_ipv6_hashkey(lchip, &ipfix_data, (DsIpfixL3Ipv6HashKey_m*)p_key);
                _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, (DsIpfixSessionRecord_m*)p_ad);
                if(is_remove)
                {
                    ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                    if(ret == CTC_E_NONE)
                    {
                        CTC_ERROR_GOTO(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, index*4+base_entry), ret, error);
                    }
                }
                else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
                {
                    entry_index = p_data->start_index;
                    fn(&ipfix_data, &entry_index);
                }
                else
                {
                    fn(&ipfix_data, p_data->user_data);
                }
                count += 2;
                if (index*4+base_entry > ipfix_max_entry_num-4)
                {
                    p_data->is_end = 1;
                    goto end_pro;
                }
                else if (count >= p_data->entry_num)
                {
                    next_index = (index+1)*4+base_entry;
                    goto end_pro;
                }
            }
            else if ((hash_type == SYS_IPFIX_HASH_TYPE_MPLS) || (hash_type1 == SYS_IPFIX_HASH_TYPE_MPLS))
            {
                ipfix_data.key_type = CTC_IPFIX_KEY_HASH_MPLS;
                for (sub_idx = 0; sub_idx < 2; sub_idx++)
                {
                    p_sub_key = ((DsIpfixL3MplsHashKey_m*)p_key + sub_idx);
                    p_sub_ad = ((DsIpfixSessionRecord_m*)p_ad + sub_idx);

                    if (GetDsIpfixL3MplsHashKey(V, hashKeyType_f, p_sub_key) != SYS_IPFIX_HASH_TYPE_INVALID)
                    {
                        _sys_goldengate_ipfix_decode_mpls_hashkey(lchip, &ipfix_data, (DsIpfixL3MplsHashKey_m*)p_sub_key);
                        _sys_goldengate_ipfix_decode_ad_info(lchip, &ipfix_data, (DsIpfixSessionRecord_m*)p_sub_ad);
                        if(is_remove)
                        {
                            ret  = ((ctc_ipfix_traverse_remove_cmp)fn)(&ipfix_data, p_data->user_data);
                            if(ret == CTC_E_NONE)
                            {
                                CTC_ERROR_GOTO(sys_goldengate_ipfix_delete_entry_by_index(lchip, ipfix_data.key_type, index*4+base_entry+sub_idx*2), ret, error);
                            }
                        }
                        else if(traverse_action == SYS_IPFIX_DUMP_ENYRY_INFO)
                        {
                            entry_index = p_data->start_index;
                            fn(&ipfix_data, &entry_index);
                        }
                        else
                        {
                            fn(&ipfix_data, p_data->user_data);
                        }
                        count++;
                    }

                    if (base_entry+index*4+2*(sub_idx +1) > ipfix_max_entry_num-2)
                    {
                        p_data->is_end = 1;
                        goto end_pro;
                    }
                    else if (count >= p_data->entry_num)
                    {
                        next_index = base_entry+index*4+2*(sub_idx +1);
                        goto end_pro;
                    }
                }
            }

        }

        if (p_data->is_end)
        {
            break;
        }
        else
        {
            base_entry += 100*4;
        }
    }while(count < p_data->entry_num);
end_pro:
    p_data->next_traverse_index = next_index;

error:
    mem_free(p_buffer);
    mem_free(p_ad_buffer);

    SYS_IPFIX_UNLOCK(lchip);
    return ret;
}

int32
sys_goldengate_ipfix_show_status(uint8 lchip, void *user_data)
{
    uint32 count = 0;
    uint8 flow_detail = 0;
    ctc_ipfix_global_cfg_t ipfix_cfg;
    char* str[2] = {"Enable", "Disable"};
    char* str1[2] = {"All packet", "Unknown packet"};
    char* str2[2] = {"Using group id", "Using vlan id"};

    SYS_IPFIX_INIT_CHECK(lchip);
    sal_memset(&ipfix_cfg, 0, sizeof(ctc_ipfix_global_cfg_t));
    CTC_ERROR_RETURN(sys_goldengate_ipfix_get_global_cfg(lchip, &ipfix_cfg));

    count = sys_goldengate_ipfix_get_flow_cnt(lchip, flow_detail);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Exist Flow Cnt(160bit)", count);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Aging Interval(s)", (ipfix_cfg.aging_interval));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Pkt Cnt Threshold", (ipfix_cfg.pkt_cnt));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %"PRIu64"\n", "Bytes Cnt Threshold", (ipfix_cfg.bytes_cnt));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Times Interval(ms)", (ipfix_cfg.times_interval));
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %s\n", "Conflict Export", (ipfix_cfg.conflict_export)?str[0]:str[1]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %s\n", "Tcp End Detect Enable", (ipfix_cfg.tcp_end_detect_en)?str[0]:str[1]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %s\n", "New Flow Export Enable", (ipfix_cfg.new_flow_export_en)?str[0]:str[1]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %s\n", "Sw Learning Enable", (ipfix_cfg.sw_learning_en)?str[0]:str[1]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %s\n", "Unkown Pkt Dest Type", (ipfix_cfg.unkown_pkt_dest_type)?str2[1]:str2[0]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %s\n", "Sample Mode", (ipfix_cfg.sample_mode)?str1[1]:str1[0]);

    SYS_IPFIX_LOCK(lchip);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Ingress Sip0 Refer Cnt", p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[0]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Ingress Sip1 Refer Cnt", p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[1]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Ingress Sip2 Refer Cnt", p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[2]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Ingress Sip3 Refer Cnt", p_gg_ipfix_master[lchip]->igr_sip_refer_cnt[3]);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Egress  Sip0 Refer Cnt", p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[0]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Egress  Sip1 Refer Cnt", p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[1]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Egress  Sip2 Refer Cnt", p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[2]);
    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-22s: %u\n", "Egress  Sip3 Refer Cnt", p_gg_ipfix_master[lchip]->egr_sip_refer_cnt[3]);
    SYS_IPFIX_UNLOCK(lchip);

    SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return CTC_E_NONE;
}

int32
sys_goldengate_ipfix_show_stats(uint8 lchip, void *user_data)
{
    char* str1[8] = {"Expired stats", "Tcp Close stats", "PktCnt Overflow stats", "ByteCnt Overflow stats",
                     "Ts Overflow stats", "Conflict stats", "New Flow stats", "Dest Change stats"};
    uint8 cnt = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    SYS_IPFIX_LOCK(lchip);
    for(cnt=0; cnt <8; cnt++)
    {
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-20s\n", str1[cnt]);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %d\n", "--Export count", p_gg_ipfix_master[lchip]->exp_cnt_stats[cnt]);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %"PRId64"\n", "--Packet count", p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[cnt]);
        SYS_IPFIX_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "%-24s: %"PRId64"\n\n", "--Packet byte", p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[cnt]);
    }
    SYS_IPFIX_UNLOCK(lchip);
    return CTC_E_NONE;
}

int32
sys_goldengate_ipfix_clear_stats(uint8 lchip, void *user_data)
{
    uint8 cnt = 0;

    SYS_IPFIX_INIT_CHECK(lchip);
    SYS_IPFIX_LOCK(lchip);
    for(cnt=0; cnt <8; cnt++)
    {
        p_gg_ipfix_master[lchip]->exp_cnt_stats[cnt] = 0;
        p_gg_ipfix_master[lchip]->exp_pkt_cnt_stats[cnt] = 0;
        p_gg_ipfix_master[lchip]->exp_pkt_byte_stats[cnt]= 0;
    }
    SYS_IPFIX_UNLOCK(lchip);
    return CTC_E_NONE;
}

#define __WARMBOOT__
int32
sys_goldengate_ipfix_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_ipfix_master_t* p_wb_ipfix_master;


    /*syncup matser*/
    wb_data.buffer = mem_malloc(MEM_IPFIX_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_DATA_T((&wb_data),sys_wb_ipfix_master_t, CTC_FEATURE_IPFIX, SYS_WB_APPID_IPFIX_SUBID_MASTER);

    p_wb_ipfix_master = (sys_wb_ipfix_master_t *)wb_data.buffer;
    p_wb_ipfix_master->lchip = lchip;

    sal_memcpy(p_wb_ipfix_master->igr_sip_refer_cnt, p_gg_ipfix_master[lchip]->igr_sip_refer_cnt, sizeof(uint16)*4);
    sal_memcpy(p_wb_ipfix_master->egr_sip_refer_cnt, p_gg_ipfix_master[lchip]->egr_sip_refer_cnt, sizeof(uint16)*4);
    wb_data.valid_cnt = 1;
    CTC_ERROR_GOTO(ctc_wb_add_entry(&wb_data), ret, done);

done:
    if (wb_data.buffer)
    {
        mem_free(wb_data.buffer);
    }
    return ret;
}

int32
sys_goldengate_ipfix_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    sys_wb_ipfix_master_t* p_wb_ipfix_master;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_IPFIX_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_ipfix_master_t, CTC_FEATURE_IPFIX, SYS_WB_APPID_IPFIX_SUBID_MASTER);

    CTC_ERROR_GOTO(ctc_wb_query_entry(&wb_query), ret, done);

    /*restore  master*/
    if (wb_query.valid_cnt != 1 || wb_query.is_end != 1)
    {
        CTC_WB_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "query ipfix master error! valid_cnt: %d, is_end: %d.\n", wb_query.valid_cnt, wb_query.is_end);
        goto done;
    }
    p_wb_ipfix_master = (sys_wb_ipfix_master_t*)wb_query.buffer;

    sal_memcpy(p_gg_ipfix_master[lchip]->igr_sip_refer_cnt, p_wb_ipfix_master->igr_sip_refer_cnt, sizeof(uint16)*4);
    sal_memcpy(p_gg_ipfix_master[lchip]->egr_sip_refer_cnt, p_wb_ipfix_master->egr_sip_refer_cnt, sizeof(uint16)*4);

    done:
    if (wb_query.buffer)
    {
        mem_free(wb_query.buffer);
    }

    return ret;
}

