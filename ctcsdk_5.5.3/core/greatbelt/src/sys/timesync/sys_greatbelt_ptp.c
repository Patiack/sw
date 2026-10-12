/**
 @file ctc_greatbelt_ptp.c

 @date 2010-6-7

 @version v2.0

  This file contains PTP sys layer function implementation
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_ptp.h"         /* ctc layer PTP ds */
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_vector.h"

#include "sys_greatbelt_ptp.h"  /* sys ds for all PTP sub modules */
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_common.h"

#include "greatbelt/include/drv_io.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/
#define SYS_PTP_MAX_NS_OR_NNS_VALUE (1000000000)
#define SYS_PTP_MAX_FRC_VALUE_SECOND (0xFFFFFFFF)
#define SYS_PTP_MAX_TOD_PULSE_HIGH_LEVEL (0x3FFFF)
#define SYS_PTP_MAX_SYNC_CLOCK_FREQUENCY_HZ (25000000)
#define SYS_PTP_MAX_SYNC_PULSE_FREQUENCY_HZ (250000)
#define SYS_PTP_MAX_SYNC_CODE_BIT (97)
#define SYS_PTP_MAX_TOD_ADJUSTING_THRESHOLD (250)
#define SYS_PTP_MAX_TOD_ADJUSTING_TOGGLE_STEP (250000000)

#define SYS_PTP_MAX_CAPTURED_TX_SEQ_ID (4)
#define SYS_PTP_MAX_CAPTURED_RX_SOURCE (9)
#define SYS_PTP_MAX_CAPTURED_RX_SEQ_ID (16)

#define SYS_PTP_MAX_TX_TS_BLOCK_NUM  4
#define SYS_PTP_MAX_TX_TS_BLOCK_SIZE  SYS_GB_MAX_PHY_PORT
#define SYS_PTP_MAX_RX_TS_BLOCK_NUM  MAX_CTC_PTP_TIEM_INTF
#define SYS_PTP_MAX_RX_TS_BLOCK_SIZE  SYS_PTP_MAX_CAPTURED_RX_SEQ_ID

#define SYS_TAI_TO_GPS_SECONDS (315964819)
#define SYS_SECONDS_OF_EACH_WEEK (604800)

#define SYS_PTP_TIEM_INTF_SYNCE_0 0
#define SYS_PTP_TIEM_INTF_SYNCE_1 1
#define SYS_PTP_TIEM_INTF_SERDES 4
#define SYS_PTP_TIEM_INTF_SYNC_PULSE 5
#define SYS_PTP_TIEM_INTF_TOD_1PPS 6
#define SYS_PTP_TIEM_INTF_GPIO_0 7
#define SYS_PTP_TIEM_INTF_GPIO_1 8

#define SYS_PTP_MAX_TOD_1PPS_DELAY (0x80000000)

#define PTP_LOCK(lchip) \
    if (p_gb_ptp_master[lchip]->p_ptp_mutex) sal_mutex_lock(p_gb_ptp_master[lchip]->p_ptp_mutex)
#define PTP_UNLOCK(lchip) \
    if (p_gb_ptp_master[lchip]->p_ptp_mutex) sal_mutex_unlock(p_gb_ptp_master[lchip]->p_ptp_mutex)

#define SYS_PTP_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(ptp, ptp, PTP_PTP_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_PTP_INIT_CHECK(lchip) \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gb_ptp_master[lchip]){ \
            return CTC_E_NOT_INIT; } \
    } while (0)

#define SYS_PTP_LOCAL_PORT_CHECK(lport) \
    do { \
        if (lport >= SYS_GB_MAX_PHY_PORT){ \
            return CTC_E_LOCAL_PORT_NOT_EXIST; } \
    } while (0)

#define SYS_PTP_RX_SOURCE_CHECK(source) \
    do { \
        if (source >= SYS_PTP_MAX_CAPTURED_RX_SOURCE){ \
            return CTC_E_LOCAL_PORT_NOT_EXIST; } \
    } while (0)

#define SYS_PTP_TX_SEQ_ID_CHECK(seq_id) \
    do { \
        if (seq_id >= SYS_PTP_MAX_CAPTURED_TX_SEQ_ID){ \
            return CTC_E_PTP_INVALID_CAPTURED_TS_SEQ_ID; } \
    } while (0)

#define SYS_PTP_RX_SEQ_ID_CHECK(seq_id) \
    do { \
        if (seq_id >= SYS_PTP_MAX_CAPTURED_RX_SEQ_ID){ \
            return CTC_E_PTP_INVALID_CAPTURED_TS_SEQ_ID; } \
    } while (0)

/****************************************************************************
*
* Data structures
*
****************************************************************************/
/**
 @brief   ptp captured timestamp
*/
struct sys_ptp_capured_ts_s
{
    uint8  lport_or_source;     /*save captured source or lport*/
    uint8  resv;
    uint16 seq_id;            /**< sequence ID*/
    uint32 system_time;
    ctc_ptp_time_t ts;
};
typedef struct sys_ptp_capured_ts_s sys_ptp_capured_ts_t;

/**
 @brief  Define sys layer ptp global configure data structure
*/
struct sys_ptp_master_s
{
    sal_mutex_t* p_ptp_mutex;
    uint8 quanta;
    uint8 intf_selected;
    uint16 cache_aging_time;  /**< aging time for TS, unit:seconds*/
    ctc_ptp_device_type_t device_type;
    ctc_vector_t* p_tx_ts_vector;
    ctc_vector_t* p_rx_ts_vector;
    CTC_INTERRUPT_EVENT_FUNC g_p_tx_ts_capture_cb;
    CTC_INTERRUPT_EVENT_FUNC g_p_tx_input_ready_cb;
    uint8 tod_1pps_duty;
    uint32 tod_1pps_delay;/**< 1pps delay between Tod master and slave*/
};
typedef struct sys_ptp_master_s sys_ptp_master_t;

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
static sys_ptp_master_t* p_gb_ptp_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
* Function
*
*****************************************************************************/
STATIC int32
_sys_greatbelt_ptp_check_ptp_init(uint8 lchip)
{
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;
    ptp_frc_quanta_t ptp_frc_quanta;

    cmd = DRV_IOR(PtpTodClkDivCfg_t, DRV_ENTRY_FLAG);
    ret = DRV_IOCTL(lchip, 0, cmd, &ptp_frc_quanta);

    return ret;
}

STATIC int32
_sys_greatbelt_ptp_get_quanta(uint8 lchip, uint8* quanta)
{
    /* !!!need get quanta from datapath!!! */
    lchip = 8;
    *quanta = lchip;
    return CTC_E_NONE;
}

/**
 @brief Initialize PTP module and set ptp default config
*/
int32
sys_greatbelt_ptp_init(uint8 lchip, ctc_ptp_global_config_t* ptp_global_cfg)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    ipe_ptp_ctl_t ipe_ptp_ctl;
    ipe_intf_mapper_ctl_t ipe_intf_mapper_ctl;
    ptp_frc_quanta_t ptp_frc_quanta;
    ptp_frc_ctl_t ptp_frc_ctl;
    ptp_sync_intf_cfg_t ptp_sync_intf_cfg;
    ptp_tod_cfg_t ptp_tod_cfg;
    ptp_engine_fifo_depth_record_t ptp_engine_fifo_depth_record;
    ptp_ts_capture_ctrl_t ptp_ts_capture_ctrl;
    uint16 coreclock    = 0;
    uint32 field_value  = 0;

    CTC_PTR_VALID_CHECK(ptp_global_cfg);

    sal_memset(&ptp_frc_quanta, 0, sizeof(ptp_frc_quanta));
    sal_memset(&ipe_ptp_ctl, 0, sizeof(ipe_ptp_ctl));
    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl));
    sal_memset(&ptp_frc_ctl, 0, sizeof(ptp_frc_ctl));
    sal_memset(&ptp_sync_intf_cfg, 0, sizeof(ptp_sync_intf_cfg));
    sal_memset(&ptp_tod_cfg, 0, sizeof(ptp_tod_cfg));
    sal_memset(&ptp_engine_fifo_depth_record, 0, sizeof(ptp_engine_fifo_depth_record));
    sal_memset(&ptp_ts_capture_ctrl, 0, sizeof(ptp_ts_capture_ctrl));

    if (CTC_E_NONE != _sys_greatbelt_ptp_check_ptp_init(lchip))
    {
        return CTC_E_NONE;
    }

    if (p_gb_ptp_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    /* create ptp master */
    p_gb_ptp_master[lchip] = (sys_ptp_master_t*)mem_malloc(MEM_PTP_MODULE, sizeof(sys_ptp_master_t));
    if (NULL == p_gb_ptp_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_ptp_master[lchip], 0, sizeof(sys_ptp_master_t));

    ret = sal_mutex_create(&(p_gb_ptp_master[lchip]->p_ptp_mutex));
    if (ret || !(p_gb_ptp_master[lchip]->p_ptp_mutex))
    {
        mem_free(p_gb_ptp_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    /*init ptp master*/


    field_value = 1;
    cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetPtpEngine_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*init ToD BaudRate*/
    field_value = 1;
    cmd = DRV_IOW(PtpTodClkDivCfg_t, PtpTodClkDivCfg_ResetTodClkDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    CTC_ERROR_RETURN(sys_greatbelt_get_chip_clock(lchip, &coreclock));
    field_value = (coreclock*1000000) / (9600*16) - 1;
    cmd = DRV_IOW(PtpTodClkDivCfg_t, PtpTodClkDivCfg_TodClkDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    cmd = DRV_IOW(PtpTodClkDivCfg_t, PtpTodClkDivCfg_ResetTodClkDiv_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetPtpEngine_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    /*init quanta*/
    CTC_ERROR_RETURN(_sys_greatbelt_ptp_get_quanta(lchip, &(p_gb_ptp_master[lchip]->quanta)));
    if (0 == p_gb_ptp_master[lchip]->quanta)
    {
        p_gb_ptp_master[lchip]->quanta = 8;
    }

    ptp_frc_quanta.quanta = p_gb_ptp_master[lchip]->quanta;
    cmd = DRV_IOW(PtpFrcQuanta_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_frc_quanta));

    /* init ts buffer */
    p_gb_ptp_master[lchip]->p_tx_ts_vector = ctc_vector_init(SYS_PTP_MAX_TX_TS_BLOCK_NUM, SYS_PTP_MAX_TX_TS_BLOCK_SIZE);
    if (NULL == p_gb_ptp_master[lchip]->p_tx_ts_vector)
    {
        return CTC_E_NO_MEMORY;
    }

    p_gb_ptp_master[lchip]->p_rx_ts_vector
    = ctc_vector_init(SYS_PTP_MAX_RX_TS_BLOCK_NUM, SYS_PTP_MAX_RX_TS_BLOCK_SIZE);
    if (NULL == p_gb_ptp_master[lchip]->p_rx_ts_vector)
    {
        return CTC_E_NO_MEMORY;
    }

    /*init ptp register*/
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl));
    ipe_ptp_ctl.ptp_ucast_disable = ptp_global_cfg->ptp_ucast_en ? 0 : 1;
    ipe_ptp_ctl.ptp_sync_snooping_en = ptp_global_cfg->sync_copy_to_cpu_en ? 1 : 0;
    ipe_ptp_ctl.ptp_management_snooping_en = ptp_global_cfg->management_copy_to_cpu_en ? 1 : 0;
    ipe_ptp_ctl.ptp_signing_snooping_en = ptp_global_cfg->signaling_copy_to_cpu_en ? 1 : 0;
    ipe_ptp_ctl.ptp_delay_discard = ptp_global_cfg->delay_request_process_en ? 0 : 1;
    cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));

    ipe_intf_mapper_ctl.use_port_ptp = ptp_global_cfg->port_based_ptp_en ? 1 : 0;
    cmd = DRV_IOW(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl));

    p_gb_ptp_master[lchip]->cache_aging_time = ptp_global_cfg->cache_aging_time;

    /*init sync interface*/
    ptp_sync_intf_cfg.sync_clock_out_en = 1;
    ptp_sync_intf_cfg.sync_pulse_out_enable = 1;
    ptp_sync_intf_cfg.sync_code_out_enable = 1;
    ptp_sync_intf_cfg.sync_pulse_in_intr_en = 0;
    ptp_sync_intf_cfg.sync_code_in_overwrite = 0;
    ptp_sync_intf_cfg.ignore_sync_code_in_rdy = 0;
    ptp_sync_intf_cfg.sync_pulse_out_num = 0;
    if (CTC_PTP_INTF_SELECT_SYNC == ptp_global_cfg->intf_selected)
    {
        ptp_sync_intf_cfg.ts_capture_en = 1;
    }

    cmd = DRV_IOW(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));

    /*init ToD interface*/
    ptp_tod_cfg.tod_code_out_enable = 1;
    ptp_tod_cfg.tod_pulse_out_enable = 1;
    ptp_tod_cfg.tod_pulse_in_intr_en = 0;
    ptp_tod_cfg.tod_code_in_overwrite = 0;
    ptp_tod_cfg.ignore_tod_code_in_rdy = 0;
    ptp_tod_cfg.tod_code_sample_threshold = 7;
    if (CTC_PTP_INTF_SELECT_TOD == ptp_global_cfg->intf_selected)
    {
        ptp_tod_cfg.tod_ts_capture_en = 1;
    }

    cmd = DRV_IOW(PtpTodCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_cfg));
    p_gb_ptp_master[lchip]->intf_selected = ptp_global_cfg->intf_selected;
    p_gb_ptp_master[lchip]->tod_1pps_delay = 0;



    /*set ts fifo depth*/
    ptp_engine_fifo_depth_record.ts_capture_fifo_fifo_depth = 15;
    ptp_engine_fifo_depth_record.mac_tx_ts_capture_fifo_fifo_depth = 255;
    cmd = DRV_IOW(PtpEngineFifoDepthRecord_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_engine_fifo_depth_record));
    ptp_ts_capture_ctrl.ts_capture_mask = 0x1FF & (~(1 << SYS_PTP_TIEM_INTF_TOD_1PPS));
    ptp_ts_capture_ctrl.ts_capture_fifo_intr_threshold = 1;
    cmd = DRV_IOW(PtpTsCaptureCtrl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_ts_capture_ctrl));

    /*enable FRC counter*/
    ptp_frc_ctl.frc_en_ref = 1;
    ptp_frc_ctl.frc_fifo_full_threshold_ref = 7;
    cmd = DRV_IOW(PtpFrcCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_frc_ctl));

    /*enable interrupt callback*/
    p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb = NULL;
    p_gb_ptp_master[lchip]->g_p_tx_ts_capture_cb = NULL;


    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ptp_free_node_data(void* node_data, void* user_data)
{
    mem_free(node_data);

    return CTC_E_NONE;
}

int32
sys_greatbelt_ptp_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_ptp_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*free rx ts vector*/
    ctc_vector_traverse(p_gb_ptp_master[lchip]->p_rx_ts_vector, (vector_traversal_fn)_sys_greatbelt_ptp_free_node_data, NULL);
    ctc_vector_release(p_gb_ptp_master[lchip]->p_rx_ts_vector);

    /*free tx ts vector*/
    ctc_vector_traverse(p_gb_ptp_master[lchip]->p_tx_ts_vector, (vector_traversal_fn)_sys_greatbelt_ptp_free_node_data, NULL);
    ctc_vector_release(p_gb_ptp_master[lchip]->p_tx_ts_vector);

    sal_mutex_destroy(p_gb_ptp_master[lchip]->p_ptp_mutex);
    mem_free(p_gb_ptp_master[lchip]);

    return CTC_E_NONE;
}

/**
 @brief Set ptp property
*/
int32
sys_greatbelt_ptp_set_global_property(uint8 lchip, ctc_ptp_global_prop_t property, uint32 value)
{
    uint32 cmd = 0;
    uint32 field_sync_value = 0;
    uint32 field_tod_value = 0;
    ipe_ptp_ctl_t ipe_ptp_ctl;
    ipe_intf_mapper_ctl_t ipe_intf_mapper_ctl;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set ptp global property, property:%d, value:%d\n", property, value);

    sal_memset(&ipe_ptp_ctl, 0, sizeof(ipe_ptp_ctl));
    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);

    /*set global property*/

    cmd = DRV_IOR(IpePtpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl));

    switch (property)
    {
        case CTC_PTP_GLOBAL_PROP_PTP_UCASE_EN:
            ipe_ptp_ctl.ptp_ucast_disable = value ? 0 : 1;
            cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
            break;

        case CTC_PTP_GLOBAL_PROP_SYNC_COPY_TO_CPU_EN:
            ipe_ptp_ctl.ptp_sync_snooping_en = value ? 1 : 0;
            cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
            break;

        case CTC_PTP_GLOBAL_PROP_SIGNALING_COPY_TO_CPU_EN:
            ipe_ptp_ctl.ptp_signing_snooping_en = value ? 1 : 0;
            cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
            break;

        case CTC_PTP_GLOBAL_PROP_MANAGEMENT_COPY_TO_CPU_EN:
            ipe_ptp_ctl.ptp_management_snooping_en = value ? 1 : 0;
            cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
            break;

        case CTC_PTP_GLOBAL_PROP_DELAY_REQUEST_PROCESS_EN:
            ipe_ptp_ctl.ptp_delay_discard = value ? 0 : 1;
            cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
            break;

        case CTC_PTP_GLOBAL_PROP_PORT_BASED_PTP_EN:
            ipe_intf_mapper_ctl.use_port_ptp = value ? 1 : 0;
            cmd = DRV_IOW(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl));
            break;

        case CTC_PTP_GLOBAL_PROP_CACHE_AGING_TIME:
            CTC_MAX_VALUE_CHECK(value, 0xFFFF);
            p_gb_ptp_master[lchip]->cache_aging_time = value;
            break;

        case CTC_PTP_GLOBAL_PROP_SYNC_OR_TOD_INPUT_SELECT:
            if (CTC_PTP_INTF_SELECT_NONE == value)
            {
                field_sync_value = 0;
                field_tod_value = 0;
            }
            else if (CTC_PTP_INTF_SELECT_SYNC == value)
            {
                field_sync_value = 1;
                field_tod_value = 0;
            }
            else if (CTC_PTP_INTF_SELECT_TOD == value)
            {
                field_sync_value = 0;
                field_tod_value = 1;
            }
            else
            {
                return CTC_E_INVALID_PARAM;
            }

            cmd = DRV_IOW(PtpSyncIntfCfg_t, PtpSyncIntfCfg_TsCaptureEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_sync_value));
            cmd = DRV_IOW(PtpTodCfg_t, PtpTodCfg_TodTsCaptureEn_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_tod_value));
            p_gb_ptp_master[lchip]->intf_selected = value;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Get ptp property
*/
int32
sys_greatbelt_ptp_get_global_property(uint8 lchip, ctc_ptp_global_prop_t property, uint32* value)
{
    uint32 cmd = 0;
    ipe_ptp_ctl_t ipe_ptp_ctl;
    ipe_intf_mapper_ctl_t ipe_intf_mapper_ctl;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(value);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get ptp global property, property:%d, value:%d\n", property, *value);

    sal_memset(&ipe_ptp_ctl, 0, sizeof(ipe_ptp_ctl));
    sal_memset(&ipe_intf_mapper_ctl, 0, sizeof(ipe_intf_mapper_ctl));

    /*get global property*/
    cmd = DRV_IOR(IpePtpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));
    cmd = DRV_IOR(IpeIntfMapperCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_intf_mapper_ctl));

    switch (property)
    {
    case CTC_PTP_GLOBAL_PROP_PTP_UCASE_EN:
        *value = ipe_ptp_ctl.ptp_ucast_disable ? 0 : 1;
        break;

    case CTC_PTP_GLOBAL_PROP_SYNC_COPY_TO_CPU_EN:
        *value = ipe_ptp_ctl.ptp_sync_snooping_en ? 1 : 0;
        break;

    case CTC_PTP_GLOBAL_PROP_SIGNALING_COPY_TO_CPU_EN:
        *value = ipe_ptp_ctl.ptp_signing_snooping_en ? 1 : 0;
        break;

    case CTC_PTP_GLOBAL_PROP_MANAGEMENT_COPY_TO_CPU_EN:
        *value = ipe_ptp_ctl.ptp_management_snooping_en ? 1 : 0;
        break;

    case CTC_PTP_GLOBAL_PROP_DELAY_REQUEST_PROCESS_EN:
        *value = ipe_ptp_ctl.ptp_delay_discard ? 0 : 1;
        break;

    case CTC_PTP_GLOBAL_PROP_PORT_BASED_PTP_EN:
        *value = ipe_intf_mapper_ctl.use_port_ptp ? 1 : 0;
        break;

    case CTC_PTP_GLOBAL_PROP_CACHE_AGING_TIME:
        *value = p_gb_ptp_master[lchip]->cache_aging_time;
        break;

    case CTC_PTP_GLOBAL_PROP_SYNC_OR_TOD_INPUT_SELECT:
        *value = p_gb_ptp_master[lchip]->intf_selected;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Get timestamp from free running clock
*/
int32
sys_greatbelt_ptp_get_clock_timestamp(uint8 lchip, ctc_ptp_time_t* timestamp)
{
    uint32 cmd = 0;
    ptp_ref_frc_t ptp_ref_frc;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(timestamp);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get ptp clock timestamp, lchip:%d\n", lchip);

    sal_memset(&ptp_ref_frc, 0, sizeof(ptp_ref_frc));

    /*read frc time*/
    cmd = DRV_IOR(PtpRefFrc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_ref_frc));

    timestamp->seconds = ptp_ref_frc.frc_second_ref;
    timestamp->nanoseconds = ptp_ref_frc.frc_ns_ref;
    timestamp->nano_nanoseconds = ptp_ref_frc.frc_frac_ns_ref;
    timestamp->is_negative = 0;

    return CTC_E_NONE;
}

/**
 @brief Adjust offset for free running clock
*/
STATIC int32
_sys_greatbelt_ptp_adjust_frc(uint8 lchip, ctc_ptp_time_t* offset)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    ptp_offset_adjust_t ptp_offset_adjust;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(offset);

    sal_memset(&ptp_offset_adjust, 0, sizeof(ptp_offset_adjust));

    /*calculate frc adjust*/
    if (offset->is_negative)  /*frc = frc - offset*/
    {
        ptp_offset_adjust.offset_ns_ref = SYS_PTP_MAX_NS_OR_NNS_VALUE - offset->nanoseconds;
        ptp_offset_adjust.offset_second_ref = SYS_PTP_MAX_FRC_VALUE_SECOND - offset->seconds;
    }
    else                       /*frc = frc + offset*/
    {
        ptp_offset_adjust.offset_second_ref = offset->seconds;
        ptp_offset_adjust.offset_ns_ref = offset->nanoseconds;
    }

    /*add quanta to offset value*/
    if ((ptp_offset_adjust.offset_ns_ref + p_gb_ptp_master[lchip]->quanta) >= SYS_PTP_MAX_NS_OR_NNS_VALUE)
    {
        ptp_offset_adjust.offset_ns_ref = ptp_offset_adjust.offset_ns_ref + p_gb_ptp_master[lchip]->quanta - SYS_PTP_MAX_NS_OR_NNS_VALUE;
        ptp_offset_adjust.offset_second_ref += 1;
    }
    else
    {
        ptp_offset_adjust.offset_ns_ref += p_gb_ptp_master[lchip]->quanta;
    }

    /*adjust frc offset*/
    cmd = DRV_IOW(PtpOffsetAdjust_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_offset_adjust));

    return ret;
}

/**
 @brief Adjust offset for free running clock
*/
int32
sys_greatbelt_ptp_adjust_clock_offset(uint8 lchip, ctc_ptp_time_t* offset)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 gps_seconds = 0;
    uint32 gps_weeks = 0;
    uint32 gps_second_time_of_week = 0;
    uint32 gps_weeks_in_seconds = 0;
    const uint32 gps_adjust_avoid_seconds_threshold = 300;
    ptp_ref_frc_t ptp_ref_frc;
    ptp_tod_tow_t ptp_tod_tow;
    ctc_ptp_time_t offset_tmp;
    ctc_ptp_time_t time;
    uint8 offset_adjust_step = 0;
    uint8 offset_adjust_remainder = 0;
    uint8 offset_adjust_num = 0;
    uint8 i = 0;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(offset);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Adjust ptp clock offset, lchip:%d, seconds:%d, nanoseconds:%d,is_negative:%d\n", \
                    lchip, offset->seconds, offset->nanoseconds, offset->is_negative);

    sal_memset(&ptp_ref_frc, 0, sizeof(ptp_ref_frc));
    sal_memset(&ptp_tod_tow, 0, sizeof(ptp_tod_tow));
    sal_memset(&offset_tmp, 0, sizeof(offset_tmp));
    sal_memset(&time, 0, sizeof(time));

    if (offset->nanoseconds >= SYS_PTP_MAX_NS_OR_NNS_VALUE)
    {
        return CTC_E_PTP_EXCEED_MAX_FRACTIONAL_VALUE;
    }

    PTP_LOCK(lchip);
    /*1. adjust clock offset*/
    if ((offset->seconds != 0) || (offset->nanoseconds > SYS_PTP_MAX_TOD_ADJUSTING_THRESHOLD)
        || (CTC_PTP_INTF_SELECT_SYNC == p_gb_ptp_master[lchip]->intf_selected))
    {
        /* adjust offset */
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_ptp_adjust_frc(lchip, offset), p_gb_ptp_master[lchip]->p_ptp_mutex);

        if(CTC_PTP_INTF_SELECT_SYNC != p_gb_ptp_master[lchip]->intf_selected)
        {
            /* read time*/
            CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_ptp_get_clock_timestamp(lchip, &time), p_gb_ptp_master[lchip]->p_ptp_mutex);
            /* set toggle time*/
            if (time.nanoseconds + SYS_PTP_MAX_TOD_ADJUSTING_TOGGLE_STEP >= SYS_PTP_MAX_NS_OR_NNS_VALUE)
            {
                time.seconds++;
                time.nanoseconds = time.nanoseconds + SYS_PTP_MAX_TOD_ADJUSTING_TOGGLE_STEP - SYS_PTP_MAX_NS_OR_NNS_VALUE;
            }
            else
            {
                time.nanoseconds = time.nanoseconds + SYS_PTP_MAX_TOD_ADJUSTING_TOGGLE_STEP;
            }
            CTC_ERROR_RETURN_WITH_UNLOCK(sys_greatbelt_ptp_set_sync_intf_toggle_time(lchip, &time), p_gb_ptp_master[lchip]->p_ptp_mutex);
        }
    }
    else
    {

        offset_adjust_step = p_gb_ptp_master[lchip]->quanta - 1;
        offset_adjust_num = offset->nanoseconds / offset_adjust_step;
        offset_adjust_remainder = offset->nanoseconds % offset_adjust_step;

        offset_tmp.is_negative = offset->is_negative;
        offset_tmp.seconds = 0;
        offset_tmp.nanoseconds = offset_adjust_step;
        for (i = 0; i < offset_adjust_num; i++)
        {
            CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_ptp_adjust_frc(lchip, &offset_tmp), p_gb_ptp_master[lchip]->p_ptp_mutex);
        }
        offset_tmp.nanoseconds = offset_adjust_remainder;
        CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_ptp_adjust_frc(lchip, &offset_tmp), p_gb_ptp_master[lchip]->p_ptp_mutex);

    }


    /*2. adjust ToW of TOD*/
    /*read frc clock*/
    sal_memset(&ptp_ref_frc, 0, sizeof(ptp_ref_frc));
    cmd = DRV_IOR(PtpRefFrc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &ptp_ref_frc), p_gb_ptp_master[lchip]->p_ptp_mutex);

    if (ptp_ref_frc.frc_second_ref >= SYS_TAI_TO_GPS_SECONDS)
    {

        /*calculate running counter value TAI seconds to GPS seconds*/
        gps_seconds = ptp_ref_frc.frc_second_ref - SYS_TAI_TO_GPS_SECONDS; /*current second from 1990*/
        gps_weeks = gps_seconds / SYS_SECONDS_OF_EACH_WEEK; /*current week from 1990*/
        gps_second_time_of_week = gps_seconds % SYS_SECONDS_OF_EACH_WEEK; /*current second from current week*/
        gps_weeks_in_seconds = ptp_ref_frc.frc_second_ref - gps_second_time_of_week; /*seconds between 1970 to 1990*/
        if ((gps_second_time_of_week > gps_adjust_avoid_seconds_threshold)
            && (gps_second_time_of_week < (SYS_SECONDS_OF_EACH_WEEK - gps_adjust_avoid_seconds_threshold)))
        {
            ptp_tod_tow.week = gps_weeks;
            ptp_tod_tow.week_to_s = gps_weeks_in_seconds;
            cmd = DRV_IOW(PtpTodTow_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_tow), p_gb_ptp_master[lchip]->p_ptp_mutex);
        }
        else
        {
            ret = CTC_E_PTP_CAN_NOT_SET_TOW;
        }
    }
    else
    {
        ret = CTC_E_PTP_TAI_LESS_THAN_GPS;
    }

    PTP_UNLOCK(lchip);

    return ret;
}

/**
 @brief Set drift for free running clock
*/
int32
sys_greatbelt_ptp_set_clock_drift(uint8 lchip, ctc_ptp_time_t* p_drift)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint64 drift_rate = 0;
    uint64 drift_ns = 0;
    ptp_drift_rate_adjust_t ptp_drift_rate_adjust;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);

    CTC_PTR_VALID_CHECK(p_drift);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set ptp clock drift, lchip:%d, nanoseconds:%d, is_negative:%d\n", \
                    lchip, p_drift->nanoseconds, p_drift->is_negative);

    sal_memset(&ptp_drift_rate_adjust, 0, sizeof(ptp_drift_rate_adjust));

    if (p_drift->nanoseconds >= SYS_PTP_MAX_NS_OR_NNS_VALUE)
    {
        return CTC_E_PTP_EXCEED_MAX_FRACTIONAL_VALUE;
    }

    PTP_LOCK(lchip);
    /*calculate drift rate,drift rate = drift * quanta*/
    drift_ns = p_drift->nanoseconds;
    drift_rate = drift_ns * p_gb_ptp_master[lchip]->quanta;

    if (drift_rate >= SYS_PTP_MAX_NS_OR_NNS_VALUE)
    {
        ret = CTC_E_PTP_EXCEED_MAX_DRIFT_VALUE;
    }
    else
    {
        ptp_drift_rate_adjust.sign_ref = p_drift->is_negative ? 1 : 0;
        ptp_drift_rate_adjust.drift_rate_ref = drift_rate & 0xFFFFFFFF;
        /*adjust frc drift */
        cmd = DRV_IOW(PtpDriftRateAdjust_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &ptp_drift_rate_adjust), p_gb_ptp_master[lchip]->p_ptp_mutex);
    }

    PTP_UNLOCK(lchip);

    return ret;
}

/**
 @brief Get drift for free running clock
*/
int32
sys_greatbelt_ptp_get_clock_drift(uint8 lchip, ctc_ptp_time_t* p_drift)
{
    uint32 cmd = 0;
    ptp_drift_rate_adjust_t ptp_drift_rate_adjust;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_drift);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get ptp clock drift, lchip:%d\n", lchip);

    sal_memset(&ptp_drift_rate_adjust, 0, sizeof(ptp_drift_rate_adjust));

    /*read frc drift*/
    cmd = DRV_IOR(PtpDriftRateAdjust_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_drift_rate_adjust));

    /*calculate drift,drift = drift_rate / quanta */
    p_drift->seconds = 0;
    p_drift->nanoseconds = ptp_drift_rate_adjust.drift_rate_ref / p_gb_ptp_master[lchip]->quanta;
    p_drift->nano_nanoseconds = 0;
    p_drift->is_negative = ptp_drift_rate_adjust.sign_ref ? 1 : 0;

    return CTC_E_NONE;
}

/**
 @brief Set PTP device type
*/
int32
sys_greatbelt_ptp_set_device_type(uint8 lchip, ctc_ptp_device_type_t device_type)
{
    uint32 cmd = 0;
    ipe_ptp_ctl_t ipe_ptp_ctl;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set ptp device type, type:%d\n", device_type);

    sal_memset(&ipe_ptp_ctl, 0, sizeof(ipe_ptp_ctl));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);



    cmd = DRV_IOR(IpePtpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));

    /*config device type*/
    switch (device_type)
    {
        case CTC_PTP_DEVICE_NONE:
            ipe_ptp_ctl.ptp_clock_type = 0;
            break;

        case CTC_PTP_DEVICE_OC:
        case CTC_PTP_DEVICE_BC:
            ipe_ptp_ctl.ptp_clock_type = 1;
            break;

        case CTC_PTP_DEVICE_E2E_TC:
            ipe_ptp_ctl.ptp_clock_type = 2;
            ipe_ptp_ctl.is_e2e_clock = 1;
            break;

        case CTC_PTP_DEVICE_P2P_TC:
            ipe_ptp_ctl.ptp_clock_type = 2;
            ipe_ptp_ctl.is_e2e_clock = 0;
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(IpePtpCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_ptp_ctl));

    p_gb_ptp_master[lchip]->device_type = device_type;

    return CTC_E_NONE;
}

/**
 @brief Set PTP device type
*/
int32
sys_greatbelt_ptp_get_device_type(uint8 lchip, ctc_ptp_device_type_t* device_type)
{
    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(device_type);

    *device_type = p_gb_ptp_master[lchip]->device_type;

    return CTC_E_NONE;
}

/**
 @brief Set adjust delay for PTP message delay correct
*/
int32
sys_greatbelt_ptp_set_adjust_delay(uint8 lchip, uint16 gport, ctc_ptp_adjust_delay_type_t type, int64 value)
{
    uint32 cmd = 0;
    uint8 lport = 0;
    uint8 chan_id = 0;
    ds_dest_ptp_chan_t dest_ptp_chan;
    ds_dest_channel_t dest_channel;
    ds_src_channel_t src_channel;
    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_PTP_LOCAL_PORT_CHECK(lport);


    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set ptp adjust delay, gport:%d, type:%d, value:%"PRIu64"\n", gport, type, value);

    sal_memset(&dest_ptp_chan, 0, sizeof(dest_ptp_chan));
    sal_memset(&dest_channel, 0, sizeof(dest_channel));
    sal_memset(&src_channel, 0, sizeof(src_channel));


    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    CTC_MAX_VALUE_CHECK(chan_id, SYS_MAX_CHANNEL_NUM-1);

    /*read delay from register*/
    cmd = DRV_IOR(DsSrcChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &src_channel));
    cmd = DRV_IOR(DsDestPtpChan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &dest_ptp_chan));
    cmd = DRV_IOR(DsDestChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &dest_channel));



    switch (type)
    {
    case CTC_PTP_ADJUST_DELAY_INGRESS_LATENCY:
        if (value < 0)
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_MAX_VALUE_CHECK(value, 0xFFFF);
        src_channel.ingress_latency = value & 0xFFFF;
        cmd = DRV_IOW(DsSrcChannel_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &src_channel));
        break;

    case CTC_PTP_ADJUST_DELAY_EGRESS_LATENCY:
        if (value < 0)
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_MAX_VALUE_CHECK(value, 0xFFFF);
        dest_ptp_chan.egress_latency = value & 0xFFFF;
        cmd = DRV_IOW(DsDestPtpChan_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &dest_ptp_chan));
        break;

    case CTC_PTP_ADJUST_DELAY_INGRESS_ASYMMETRY:
        src_channel.asymmetry_delay_negtive = (value >= 0) ? 0 : 1;
        if (value < 0)
        {
            value = -value;
        }

        if (((value >> 32) & 0xFFFFFFFF) > 0xF)
        {
            return CTC_E_INVALID_PARAM;
        }

        src_channel.asymmetry_delay35_32 = (value >> 32) & 0xF;
        src_channel.asymmetry_delay31_0  = value & 0xFFFFFFFF;
        cmd = DRV_IOW(DsSrcChannel_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &src_channel));
        break;

    case CTC_PTP_ADJUST_DELAY_EGRESS_ASYMMETRY:
        dest_channel.asymmetry_delay_negtive = (value >= 0) ? 0 : 1;
        if (value < 0)
        {
            value = -value;
        }

        if (((value >> 32) & 0xFFFFFFFF) > 0xF)
        {
            return CTC_E_INVALID_PARAM;
        }

        dest_channel.asymmetry_delay35_32 = (value >> 32) & 0xF;
        dest_channel.asymmetry_delay31_0  = value & 0xFFFFFFFF;
        cmd = DRV_IOW(DsDestChannel_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &dest_channel));
        break;

    case CTC_PTP_ADJUST_DELAY_PATH_DELAY:
        if ((((value >> 32) & 0xFFFFFFFF) > 0xF) || (value < 0))
        {
            return CTC_E_INVALID_PARAM;
        }

        src_channel.path_delay35_32 = (value >> 32) & 0xF;
        src_channel.path_delay31_0  = value & 0xFFFFFFFF;
        cmd = DRV_IOW(DsSrcChannel_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &src_channel));
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief Set adjust delay for PTP message delay correct
*/
int32
sys_greatbelt_ptp_get_adjust_delay(uint8 lchip, uint16 gport, ctc_ptp_adjust_delay_type_t type, int64* value)
{
    uint32 cmd = 0;
    uint8 lport = 0;
    uint8 chan_id = 0;

    ds_dest_ptp_chan_t dest_ptp_chan;
    ds_dest_channel_t dest_channel;
    ds_src_channel_t src_channel;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get ptp adjust delay, gport:%d, type:%d\n", gport, type);

    sal_memset(&dest_ptp_chan, 0, sizeof(dest_ptp_chan));
    sal_memset(&dest_channel, 0, sizeof(dest_channel));
    sal_memset(&src_channel, 0, sizeof(src_channel));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);
    SYS_PTP_LOCAL_PORT_CHECK(lport);
    CTC_PTR_VALID_CHECK(value);

    chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    CTC_MAX_VALUE_CHECK(chan_id, SYS_MAX_CHANNEL_NUM-1);


    /*read delay from register*/
    cmd = DRV_IOR(DsSrcChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &src_channel));
    cmd = DRV_IOR(DsDestPtpChan_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &dest_ptp_chan));
    cmd = DRV_IOR(DsDestChannel_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, chan_id, cmd, &dest_channel));

    switch (type)
    {
    case CTC_PTP_ADJUST_DELAY_INGRESS_LATENCY:
        *value = src_channel.ingress_latency;
        break;

    case CTC_PTP_ADJUST_DELAY_EGRESS_LATENCY:
        *value = dest_ptp_chan.egress_latency;
        break;

    case CTC_PTP_ADJUST_DELAY_INGRESS_ASYMMETRY:
        *value = src_channel.asymmetry_delay35_32;
        *value = (*value << 32) + src_channel.asymmetry_delay31_0;
        if (src_channel.asymmetry_delay_negtive)
        {
            *value = -(*value);
        }

        break;

    case CTC_PTP_ADJUST_DELAY_EGRESS_ASYMMETRY:
        *value = dest_channel.asymmetry_delay35_32;
        *value = (*value << 32) + dest_channel.asymmetry_delay31_0;
        if (dest_channel.asymmetry_delay_negtive)
        {
            *value = -(*value);
        }

        break;

    case CTC_PTP_ADJUST_DELAY_PATH_DELAY:
        *value = src_channel.path_delay35_32;
        *value = (*value << 32) + src_channel.path_delay31_0;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

/**
 @brief  Set sync interface config
*/
int32
sys_greatbelt_ptp_set_sync_intf(uint8 lchip, ctc_ptp_sync_intf_cfg_t* p_config)
{
    uint32 cmd = 0;
    ptp_sync_intf_cfg_t ptp_sync_intf_cfg;
    ptp_sync_intf_half_period_t ptp_sync_intf_half_period;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set sync interface config, lchip:%d\n", lchip);

    sal_memset(&ptp_sync_intf_cfg, 0, sizeof(ptp_sync_intf_cfg));
    sal_memset(&ptp_sync_intf_half_period, 0, sizeof(ptp_sync_intf_half_period));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_config);

    if ((p_config->sync_clock_hz > SYS_PTP_MAX_SYNC_CLOCK_FREQUENCY_HZ) || (p_config->sync_clock_hz < 1))
    {
        return CTC_E_PTP_SYNC_SIGNAL_FREQUENCY_OUT_OF_RANGE;
    }

    if ((p_config->sync_pulse_hz > SYS_PTP_MAX_SYNC_PULSE_FREQUENCY_HZ) || (p_config->sync_pulse_hz < 1))
    {
        return CTC_E_PTP_SYNC_SIGNAL_FREQUENCY_OUT_OF_RANGE;
    }

    if ((p_config->sync_pulse_duty > 99) || (p_config->sync_pulse_duty < 1))
    {
        return CTC_E_PTP_SYNC_SIGNAL_DUTY_OUT_OF_RANGE;
    }

    if (((p_config->accuracy > 49) || (p_config->accuracy < 32)) && (p_config->accuracy != 0xFE))
    {
        return CTC_E_PTP_INVALID_SYNC_INTF_ACCURACY_VALUE;
    }

    if (p_config->epoch >= 63)
    {
        return CTC_E_PTP_INVALID_SYNC_INTF_EPOCH_VALUE;
    }

    /*the frequency relationship between syncClock and syncPulse must set as follow:
    (1)the frequency of syncClock must be integer multiples of syncPulse
    (2)the period of syncClock must be longer than ( epoch + SYS_PTP_MAX_SYNC_CODE_BIT )
    */
    if ((p_config->sync_pulse_hz > p_config->sync_clock_hz)
        || (p_config->sync_clock_hz % p_config->sync_pulse_hz)
        || ((p_config->sync_clock_hz / p_config->sync_pulse_hz) < (p_config->epoch + SYS_PTP_MAX_SYNC_CODE_BIT)))
    {
        return CTC_E_PTP_SYNC_SIGNAL_NOT_VALID;
    }

    /*set sync interface as input mode*/
    cmd = DRV_IOR(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));
    ptp_sync_intf_cfg.sync_intf_mode = 0;
    cmd = DRV_IOW(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));

    /*set sync interface config*/
    ptp_sync_intf_cfg.sync_intf_mode = p_config->mode ? 1 : 0;
    ptp_sync_intf_cfg.lock = p_config->lock ? 1 : 0;
    ptp_sync_intf_cfg.accuracy = p_config->accuracy;
    ptp_sync_intf_cfg.epoch = p_config->epoch;

    /*calculate syncClock half period and syncPulse period&duty*/
    ptp_sync_intf_half_period.half_period_ns = SYS_PTP_MAX_NS_OR_NNS_VALUE / (p_config->sync_clock_hz * 2);
    ptp_sync_intf_cfg.sync_pulse_out_period = p_config->sync_clock_hz / p_config->sync_pulse_hz;
    ptp_sync_intf_cfg.sync_pulse_out_threshold = (ptp_sync_intf_cfg.sync_pulse_out_period * p_config->sync_pulse_duty) / 100;

    cmd = DRV_IOW(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));
    cmd = DRV_IOW(PtpSyncIntfHalfPeriod_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_half_period));

    return CTC_E_NONE;
}

/**
 @brief  Get sync interface config
*/
int32
sys_greatbelt_ptp_get_sync_intf(uint8 lchip, ctc_ptp_sync_intf_cfg_t* p_config)
{
    uint32 cmd = 0;
    ptp_sync_intf_cfg_t ptp_sync_intf_cfg;
    ptp_sync_intf_half_period_t ptp_sync_intf_half_period;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get sync interface config, lchip:%d\n", lchip);

    sal_memset(&ptp_sync_intf_cfg, 0, sizeof(ptp_sync_intf_cfg));
    sal_memset(&ptp_sync_intf_half_period, 0, sizeof(ptp_sync_intf_half_period));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_config);

    /*set sync interface config*/
    cmd = DRV_IOR(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));
    cmd = DRV_IOR(PtpSyncIntfHalfPeriod_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_half_period));

    p_config->mode = ptp_sync_intf_cfg.sync_intf_mode ? 1 : 0;
    p_config->lock = ptp_sync_intf_cfg.lock ? 1 : 0;
    p_config->accuracy = ptp_sync_intf_cfg.accuracy;
    p_config->epoch = ptp_sync_intf_cfg.epoch;
    if (0 == ptp_sync_intf_half_period.half_period_ns)
    {
        p_config->sync_clock_hz = 0;
    }
    else
    {
        p_config->sync_clock_hz = SYS_PTP_MAX_NS_OR_NNS_VALUE / (ptp_sync_intf_half_period.half_period_ns * 2);
    }

    if (0 == ptp_sync_intf_cfg.sync_pulse_out_period)
    {
        p_config->sync_pulse_hz = 0;
        p_config->sync_pulse_duty = 0;
    }
    else
    {
        p_config->sync_pulse_hz = p_config->sync_clock_hz / ptp_sync_intf_cfg.sync_pulse_out_period;
        p_config->sync_pulse_duty = (ptp_sync_intf_cfg.sync_pulse_out_threshold * 100) / ptp_sync_intf_cfg.sync_pulse_out_period;
    }

    return CTC_E_NONE;
}

/**
 @brief  Set toggle time to trigger sync interface output syncColck signal
*/
int32
sys_greatbelt_ptp_set_sync_intf_toggle_time(uint8 lchip, ctc_ptp_time_t* p_toggle_time)
{
    uint32 cmd = 0;
    uint8  value;
    ptp_sync_intf_toggle_t ptp_sync_intf_toggle;
    ptp_sync_intf_cfg_t ptp_sync_intf_cfg;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_toggle_time);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set sync interface toggle time, lchip:%d, seconds:%d, nanoseconds:%d, nano nanoseconds:%d\n", \
                    lchip, p_toggle_time->seconds, p_toggle_time->nanoseconds, p_toggle_time->nano_nanoseconds);

    sal_memset(&ptp_sync_intf_toggle, 0, sizeof(ptp_sync_intf_toggle));
    sal_memset(&ptp_sync_intf_cfg, 0, sizeof(ptp_sync_intf_cfg));

    if (p_toggle_time->nanoseconds >= SYS_PTP_MAX_NS_OR_NNS_VALUE
        || p_toggle_time->nano_nanoseconds >= SYS_PTP_MAX_NS_OR_NNS_VALUE)
    {
        return CTC_E_PTP_EXCEED_MAX_FRACTIONAL_VALUE;
    }

    /*set sync interface as input mode*/
    cmd = DRV_IOR(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));
    value = ptp_sync_intf_cfg.sync_intf_mode;
    ptp_sync_intf_cfg.sync_intf_mode = 0;
    cmd = DRV_IOW(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));

    /*set toggle time*/
    ptp_sync_intf_toggle.toggle_second  = p_toggle_time->seconds;
    ptp_sync_intf_toggle.toggle_ns      = p_toggle_time->nanoseconds;
    ptp_sync_intf_toggle.toggle_frac_ns = p_toggle_time->nano_nanoseconds;
    cmd = DRV_IOW(PtpSyncIntfToggle_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_toggle));

    /*set sync interface as output mode*/
    ptp_sync_intf_cfg.sync_intf_mode = value;
    cmd = DRV_IOW(PtpSyncIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_cfg));

    return CTC_E_NONE;
}

/**
 @brief  Set toggle time to trigger sync interface output syncColck signal
*/
int32
sys_greatbelt_ptp_get_sync_intf_toggle_time(uint8 lchip, ctc_ptp_time_t* p_toggle_time)
{
    uint32 cmd = 0;
    ptp_sync_intf_toggle_t ptp_sync_intf_toggle;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get sync interface toggle time, lchip:%d\n", lchip);

    sal_memset(&ptp_sync_intf_toggle, 0, sizeof(ptp_sync_intf_toggle));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_toggle_time);

    /*read toggle time*/
    cmd = DRV_IOR(PtpSyncIntfToggle_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_sync_intf_toggle));

    p_toggle_time->seconds = ptp_sync_intf_toggle.toggle_second;
    p_toggle_time->nanoseconds = ptp_sync_intf_toggle.toggle_ns;
    p_toggle_time->nano_nanoseconds = ptp_sync_intf_toggle.toggle_frac_ns;
    p_toggle_time->is_negative = 0;

    return CTC_E_NONE;
}

/**
 @brief  Get sync interface input time information
*/
int32
sys_greatbelt_ptp_get_sync_intf_code(uint8 lchip, ctc_ptp_sync_intf_code_t* g_time_code)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    ptp_sync_intf_input_code_t input_code;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get sync interface time code, lchip:%d\n", lchip);

    sal_memset(&input_code, 0, sizeof(input_code));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(g_time_code);

    PTP_LOCK(lchip);
    cmd = DRV_IOR(PtpSyncIntfInputCode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &input_code), p_gb_ptp_master[lchip]->p_ptp_mutex);
    if (1 == input_code.sync_code_in_rdy) /*timecode has received already*/
    {
        /*get timecode*/
        g_time_code->seconds      = (input_code.data88_to64 & 0xffffff) << 24
            | (input_code.data63_to32 & 0xffffff00) >> 8;
        g_time_code->nanoseconds  = (input_code.data63_to32 & 0xff) << 24
            | (input_code.data31_to0 & 0xffffff00) >> 8;
        g_time_code->lock         = input_code.data88_to64 >> 24;
        g_time_code->accuracy     = input_code.data31_to0 & 0xff;
        g_time_code->crc_error    = input_code.crc_err;
        g_time_code->crc          = input_code.crc;

        /*clear ready flag*/
        input_code.sync_code_in_rdy = 0;
        cmd = DRV_IOW(PtpSyncIntfInputCode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &input_code), p_gb_ptp_master[lchip]->p_ptp_mutex);
    }
    else
    {
        ret = CTC_E_PTP_RX_TS_INFORMATION_NOT_READY;
    }

    PTP_UNLOCK(lchip);

    return ret;

}

/**
 @brief  Clear sync interface input time information
*/
int32
sys_greatbelt_ptp_clear_sync_intf_code(uint8 lchip)
{
    uint32 cmd = 0;
    ptp_sync_intf_input_code_t input_code;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Clear sync interface time code, lchip:%d\n", lchip);

    sal_memset(&input_code, 0, sizeof(input_code));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);

    /*clear ready flag,clear intf_code.sync_code_in_rdy*/
    cmd = DRV_IOW(PtpSyncIntfInputCode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &input_code));

    return CTC_E_NONE;
}

/**
 @brief  Set ToD interface config
*/
int32
sys_greatbelt_ptp_set_tod_intf(uint8 lchip, ctc_ptp_tod_intf_cfg_t* config)
{
    uint32 cmd = 0;
    ptp_tod_cfg_t ptp_tod_cfg;
    uint16 coreclock = 0;
    ptp_captured_adj_frc_t ptp_captured_adj_frc;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set tod interface config, lchip:%d\n", lchip);

    sal_memset(&ptp_tod_cfg, 0, sizeof(ptp_tod_cfg));
    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(config);

    if ((config->pulse_duty > 99) || (config->pulse_duty < 1))
    {
        return CTC_E_PTP_SYNC_SIGNAL_DUTY_OUT_OF_RANGE;
    }

    if (config->epoch >= 0xFFF)
    {
        return CTC_E_PTP_INVALID_TOD_INTF_EPOCH_VALUE;
    }

    /*set GPIO 12, 13 use as tod pin*/
    sys_greatbelt_chip_set_gpio_mode(lchip, 12, CTC_CHIP_SPECIAL_MODE);
    sys_greatbelt_chip_set_gpio_mode(lchip, 13, CTC_CHIP_SPECIAL_MODE);

    /*set ToD interface config*/
    cmd = DRV_IOR(PtpTodCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_cfg));
    ptp_tod_cfg.tod_intf_mode = config->mode ? 1 : 0;
    ptp_tod_cfg.tod_pulse_out_enable = config->mode ? 1 : 0;
    ptp_tod_cfg.tod_ts_capture_en = config->mode ? 0 : 1;
    ptp_tod_cfg.tod_code_out_enable = config->mode ? 1 : 0;
    ptp_tod_cfg.tod_tx_stop_bit_num = config->stop_bit_num;
    ptp_tod_cfg.tod_epoch = config->epoch;

    /*1ms= todPulseOutHighPeriod*1024/clockCore(M)/1000*/
    CTC_ERROR_RETURN(sys_greatbelt_get_chip_clock(lchip, &coreclock));
    ptp_tod_cfg.tod_pulse_out_high_period = (coreclock*10000*config->pulse_duty) >> 10;
    cmd = DRV_IOW(PtpTodCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_cfg));
    p_gb_ptp_master[lchip]->tod_1pps_duty = config->pulse_duty;
    p_gb_ptp_master[lchip]->tod_1pps_delay = config->pulse_delay;

    /*for input mode need clear first*/
    if(!config->mode)
    {
        sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
        cmd = DRV_IOR(PtpCapturedAdjFrc_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));

        while (ptp_captured_adj_frc.adj_frc_bitmap) /*the FIFO is empty when the bitmap is 0*/
        {
            sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));
        }
    }

    return CTC_E_NONE;
}

/**
 @brief  Get ToD interface config
*/
int32
sys_greatbelt_ptp_get_tod_intf(uint8 lchip, ctc_ptp_tod_intf_cfg_t* config)
{
    uint32 cmd = 0;
    ptp_tod_cfg_t ptp_tod_cfg;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get tod interface config, lchip:%d\n", lchip);

    sal_memset(&ptp_tod_cfg, 0, sizeof(ptp_tod_cfg));
    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(config);

    /*get ToD interface config*/
    cmd = DRV_IOR(PtpTodCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_cfg));

    config->mode = ptp_tod_cfg.tod_intf_mode ? 1 : 0;
    config->stop_bit_num = ptp_tod_cfg.tod_tx_stop_bit_num;
    config->epoch = ptp_tod_cfg.tod_epoch;
    config->pulse_duty = p_gb_ptp_master[lchip]->tod_1pps_duty;
    config->pulse_delay = p_gb_ptp_master[lchip]->tod_1pps_delay;

    return CTC_E_NONE;
}

/**
 @brief  Set ToD interface output message config
*/
int32
sys_greatbelt_ptp_set_tod_intf_tx_code(uint8 lchip, ctc_ptp_tod_intf_code_t* tx_code)
{
    uint32 cmd = 0;
    ptp_tod_code_cfg_t ptp_tod_code_cfg;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set tod interface tx time code, lchip:%d\n", lchip);

    sal_memset(&ptp_tod_code_cfg, 0, sizeof(ptp_tod_code_cfg));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(tx_code);


    if (tx_code->clock_src > 3)
    {
        return CTC_E_PTP_INVALID_TOD_INTF_CLOCK_SRC_VALUE;
    }

    ptp_tod_code_cfg.tod_clock_src = tx_code->clock_src;
    ptp_tod_code_cfg.tod_clock_src_status = tx_code->clock_src_status;
    ptp_tod_code_cfg.tod_leap_s = tx_code->leap_second;
    ptp_tod_code_cfg.tod_monitor_alarm = tx_code->monitor_alarm;
    ptp_tod_code_cfg.tod_msg_class = tx_code->msg_class;
    ptp_tod_code_cfg.tod_msg_id = tx_code->msg_id;
    ptp_tod_code_cfg.tod_payload_len = tx_code->msg_length;
    ptp_tod_code_cfg.tod_pulse_status = tx_code->pps_status;
    ptp_tod_code_cfg.tod_tacc = tx_code->pps_accuracy;

    cmd = DRV_IOW(PtpTodCodeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_code_cfg));

    return CTC_E_NONE;
}

/**
 @brief  Get ToD interface output message config
*/
int32
sys_greatbelt_ptp_get_tod_intf_tx_code(uint8 lchip, ctc_ptp_tod_intf_code_t* tx_code)
{
    uint32 cmd = 0;
    ptp_tod_code_cfg_t ptp_tod_code_cfg;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get tod interface tx time code, lchip:%d\n", lchip);

    sal_memset(&ptp_tod_code_cfg, 0, sizeof(ptp_tod_code_cfg));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(tx_code);

    cmd = DRV_IOR(PtpTodCodeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_tod_code_cfg));

    tx_code->clock_src = ptp_tod_code_cfg.tod_clock_src;
    tx_code->clock_src_status = ptp_tod_code_cfg.tod_clock_src_status;
    tx_code->leap_second = ptp_tod_code_cfg.tod_leap_s;
    tx_code->monitor_alarm = ptp_tod_code_cfg.tod_monitor_alarm;
    tx_code->msg_class = ptp_tod_code_cfg.tod_msg_class;
    tx_code->msg_id = ptp_tod_code_cfg.tod_msg_id;
    tx_code->msg_length = ptp_tod_code_cfg.tod_payload_len;
    tx_code->pps_status = ptp_tod_code_cfg.tod_pulse_status;
    tx_code->pps_accuracy = ptp_tod_code_cfg.tod_tacc;

    return CTC_E_NONE;
}

/**
 @brief  Get ToD interface input time information
*/
int32
sys_greatbelt_ptp_get_tod_intf_rx_code(uint8 lchip, ctc_ptp_tod_intf_code_t* rx_code)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    ptp_tod_input_code_t input_code;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get tod interface rx time code, lchip:%d\n", lchip);

    sal_memset(&input_code, 0, sizeof(input_code));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(rx_code);

    PTP_LOCK(lchip);
    cmd = DRV_IOR(PtpTodInputCode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &input_code), p_gb_ptp_master[lchip]->p_ptp_mutex);

    if (1 == input_code.tod_code_in_rdy) /*message has received already*/
    {
        /*get timecode*/
        rx_code->msg_char0                  = 'C';
        rx_code->msg_char1                  = 'M';
        rx_code->msg_class                  = input_code.tod_rcv_msg_byte0;
        rx_code->msg_id                     = input_code.tod_rcv_msg_byte1;
        rx_code->msg_length                 = (input_code.tod_rcv_msg_byte2 << 8)
            + (input_code.tod_rcv_msg_byte3);
        rx_code->gps_second_time_of_week    = (input_code.tod_rcv_msg_byte4 << 24)
            + (input_code.tod_rcv_msg_byte5 << 16)
            + (input_code.tod_rcv_msg_byte6 << 8)
            + (input_code.tod_rcv_msg_byte7);
        rx_code->gps_week                   = (input_code.tod_rcv_msg_byte12 << 8)
            + (input_code.tod_rcv_msg_byte13);
        rx_code->leap_second                = (int32)input_code.tod_rcv_msg_byte14;
        rx_code->pps_status                 = input_code.tod_rcv_msg_byte15;
        rx_code->pps_accuracy               = input_code.tod_rcv_msg_byte16;
        rx_code->crc                        = input_code.tod_rcv_msg_crc;
        rx_code->crc_error                  = input_code.tod_rcv_msg_crc_err;
        /*clear ready flag*/
        input_code.tod_code_in_rdy = 0;
        cmd = DRV_IOW(PtpTodInputCode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &input_code), p_gb_ptp_master[lchip]->p_ptp_mutex);
    }
    else
    {
        ret = CTC_E_PTP_RX_TS_INFORMATION_NOT_READY;
    }

    PTP_UNLOCK(lchip);

    return ret;
}

/**
 @brief  Clear TOD interface input time information
*/
int32
sys_greatbelt_ptp_clear_tod_intf_code(uint8 lchip)
{
    uint32 cmd = 0;
    ptp_tod_input_code_t input_code;

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Clear tod interface rx time code, lchip:%d\n", lchip);

    sal_memset(&input_code, 0, sizeof(input_code));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);

    /*clear ready flag,clear intf_code.tod_code_in_rdy*/
    cmd = DRV_IOW(PtpTodInputCode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &input_code));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ptp_add_ts_cache(uint8 lchip, ctc_ptp_captured_type_t type, sys_ptp_capured_ts_t* p_captured_ts)
{
    uint32 index = 0;
    int32 ret = CTC_E_NONE;
    sal_systime_t systime;
    sys_ptp_capured_ts_t* p_ts_node = NULL;
    sys_ptp_capured_ts_t* p_ts_node_get = NULL;
    bool add_cache = TRUE;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_captured_ts);

    /*get systime*/
    sal_gettime(&systime);

    p_ts_node = (sys_ptp_capured_ts_t*)mem_malloc(MEM_PTP_MODULE, sizeof(sys_ptp_capured_ts_t));

    if (NULL == p_ts_node)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(p_ts_node, p_captured_ts, sizeof(sys_ptp_capured_ts_t));
    p_ts_node->system_time = systime.tv_sec;

    if (CTC_PTP_CAPTURED_TYPE_RX == type)
    {
        index = (p_ts_node->lport_or_source % MAX_CTC_PTP_TIEM_INTF) * SYS_PTP_MAX_RX_TS_BLOCK_SIZE + p_ts_node->seq_id;
        p_ts_node_get = (sys_ptp_capured_ts_t*)ctc_vector_get(p_gb_ptp_master[lchip]->p_rx_ts_vector, index);
        if (p_ts_node_get != NULL)
        {
            mem_free(p_ts_node_get);
        }

        add_cache = ctc_vector_add(p_gb_ptp_master[lchip]->p_rx_ts_vector, index, p_ts_node);
        if (!add_cache)
        {
            mem_free(p_ts_node);
            ret = CTC_E_PTP_TS_ADD_BUFF_FAIL;
        }
    }
    else if (CTC_PTP_CAPTURED_TYPE_TX == type)
    {
        index = p_ts_node->seq_id * SYS_PTP_MAX_TX_TS_BLOCK_SIZE + p_ts_node->lport_or_source;
        p_ts_node_get = (sys_ptp_capured_ts_t*)ctc_vector_get(p_gb_ptp_master[lchip]->p_tx_ts_vector, index);
        if (p_ts_node_get != NULL)
        {
            mem_free(p_ts_node_get);
        }

        add_cache = ctc_vector_add(p_gb_ptp_master[lchip]->p_tx_ts_vector, index, p_ts_node);
        if (!add_cache)
        {
            mem_free(p_ts_node);
            ret = CTC_E_PTP_TS_ADD_BUFF_FAIL;
        }
    }
    else
    {
        mem_free(p_ts_node);
        ret = CTC_E_PTP_INVALID_CAPTURED_TYPE;
    }

    return ret;

}

STATIC int32
_sys_greatbelt_ptp_get_ts_cache(uint8 lchip, ctc_ptp_captured_type_t type, sys_ptp_capured_ts_t* p_captured_ts)
{
    uint32 index = 0;
    sal_systime_t systime;
    sys_ptp_capured_ts_t* p_ts_node = NULL;

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_captured_ts);

    /*get systime*/
    sal_gettime(&systime);

    if (CTC_PTP_CAPTURED_TYPE_RX == type)
    {
        index = (p_captured_ts->lport_or_source % MAX_CTC_PTP_TIEM_INTF) * SYS_PTP_MAX_RX_TS_BLOCK_SIZE + p_captured_ts->seq_id;
        p_ts_node = (sys_ptp_capured_ts_t*)ctc_vector_get(p_gb_ptp_master[lchip]->p_rx_ts_vector, index);
        if (NULL == p_ts_node)
        {
            return CTC_E_PTP_TS_GET_BUFF_FAIL;
        }

        sal_memcpy(p_captured_ts, p_ts_node, sizeof(sys_ptp_capured_ts_t));

    }
    else if (CTC_PTP_CAPTURED_TYPE_TX == type)
    {
        index = p_captured_ts->seq_id * SYS_PTP_MAX_TX_TS_BLOCK_SIZE + p_captured_ts->lport_or_source;
        p_ts_node = (sys_ptp_capured_ts_t*)ctc_vector_get(p_gb_ptp_master[lchip]->p_tx_ts_vector, index);
        if (NULL == p_ts_node)
        {
            return CTC_E_PTP_TS_GET_BUFF_FAIL;
        }

        sal_memcpy(p_captured_ts, p_ts_node, sizeof(sys_ptp_capured_ts_t));
    }
    else
    {
        return CTC_E_PTP_INVALID_CAPTURED_TYPE;
    }

    /* the ts get form cache has already time out*/
    if (systime.tv_sec - p_captured_ts->system_time >= p_gb_ptp_master[lchip]->cache_aging_time)
    {
        return CTC_E_PTP_TS_GET_BUFF_FAIL;
    }

    return CTC_E_NONE;

}

/*calculate port_id and sqe_id*/
STATIC int32
_sys_greatbelt_ptp_get_tx_ts_info(uint8 lchip, ptp_mac_tx_capture_ts_t ts, uint8* port_id, uint8* seq_id)
{
    uint64 mac_tx_bitmap = 0;
    uint64 mac_tx_sqe_id = 0;
    uint8 index = 0;
    uint8 mac_id = 0;

    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(port_id);
    CTC_PTR_VALID_CHECK(seq_id);

    if ((0 == ts.mac_tx_bitmap1) && (0 == ts.mac_tx_bitmap0))
    {
        return CTC_E_PTP_TS_NOT_READY;
    }

    mac_tx_bitmap = ts.mac_tx_bitmap1;
    mac_tx_bitmap = (mac_tx_bitmap << 32) + ts.mac_tx_bitmap0;

    for (index = 0; index < SYS_GB_MAX_PHY_PORT; index++)
    {
        if ((mac_tx_bitmap >> index) & 0x01)
        {
            mac_id = index;
            break;
        }
    }

    if (mac_id >= 32)
    {
        mac_tx_sqe_id = ts.mac_tx_seq_id3;
        mac_tx_sqe_id = (mac_tx_sqe_id << 32) + ts.mac_tx_seq_id2;
        *seq_id = (mac_tx_sqe_id >> ((mac_id - 32) * 2)) & 0x03;
    }
    else
    {
        mac_tx_sqe_id = ts.mac_tx_seq_id1;
        mac_tx_sqe_id = (mac_tx_sqe_id << 32) + ts.mac_tx_seq_id0;
        *seq_id = (mac_tx_sqe_id >> (mac_id * 2)) & 0x03;
    }

    *port_id = sys_greatebelt_common_get_lport_with_mac(lchip, mac_id);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ptp_get_captured_ts(uint8 lchip, ctc_ptp_captured_type_t type, sys_ptp_capured_ts_t* p_cap_ts)
{
    uint32 cmd;
    uint8 src_bit = 0;
    bool is_fifo_match = FALSE;
    uint8 port_id = 0;
    uint8 seq_id = 0;
    ptp_captured_adj_frc_t ptp_captured_adj_frc;
    ptp_mac_tx_capture_ts_t ptp_mac_tx_capture_ts;
    sys_ptp_capured_ts_t capured_ts;

    sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc));
    sal_memset(&ptp_mac_tx_capture_ts, 0, sizeof(ptp_mac_tx_capture_ts));
    sal_memset(&capured_ts, 0, sizeof(capured_ts));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_cap_ts);

    if (CTC_PTP_CAPTURED_TYPE_RX == type)
    {
        SYS_PTP_RX_SOURCE_CHECK(p_cap_ts->lport_or_source);
        SYS_PTP_RX_SEQ_ID_CHECK(p_cap_ts->seq_id);
        cmd = DRV_IOR(PtpCapturedAdjFrc_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));
    }
    else if (CTC_PTP_CAPTURED_TYPE_TX == type)
    {
        SYS_PTP_LOCAL_PORT_CHECK(p_cap_ts->lport_or_source);
        SYS_PTP_TX_SEQ_ID_CHECK(p_cap_ts->seq_id);
        cmd = DRV_IOR(PtpMacTxCaptureTs_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_mac_tx_capture_ts));
    }
    else
    {
        return CTC_E_PTP_INVALID_CAPTURED_TYPE;
    }

    /*1. read ts from FIFO until the FIFO is empty*/
    if (CTC_PTP_CAPTURED_TYPE_RX == type)
    {
        while (ptp_captured_adj_frc.adj_frc_bitmap) /*the FIFO is empty when the bitmap is 0*/
        {
            if ((p_cap_ts->seq_id == ptp_captured_adj_frc.adj_frc_seq_id)
                && ((ptp_captured_adj_frc.adj_frc_bitmap >> p_cap_ts->lport_or_source) & 0x1)) /*seq_id and source are all match*/
            {
                p_cap_ts->ts.seconds      = ptp_captured_adj_frc.adj_frc_second;
                p_cap_ts->ts.nanoseconds  = ptp_captured_adj_frc.adj_frc_ns;
                is_fifo_match   = TRUE;
            }

            /* add ts to cache*/
            for (src_bit = 0; src_bit < 9; src_bit++) /*get captured source from bitmap*/
            {
                if ((ptp_captured_adj_frc.adj_frc_bitmap >> src_bit) & 0x01)
                {
                    break;
                }
            }

            capured_ts.lport_or_source = src_bit;
            capured_ts.seq_id       = ptp_captured_adj_frc.adj_frc_seq_id;
            capured_ts.ts.seconds   = ptp_captured_adj_frc.adj_frc_second;
            capured_ts.ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;
            CTC_ERROR_RETURN(_sys_greatbelt_ptp_add_ts_cache(lchip, type, &capured_ts)); /*add ts to cache*/

            sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));
        }
    }
    else if (CTC_PTP_CAPTURED_TYPE_TX == type)
    {
        while ((ptp_mac_tx_capture_ts.mac_tx_bitmap1 != 0) || (ptp_mac_tx_capture_ts.mac_tx_bitmap0 != 0))
        {
            if (CTC_E_NONE == _sys_greatbelt_ptp_get_tx_ts_info(lchip, ptp_mac_tx_capture_ts, &port_id, &seq_id)) /*calculate port_id and sqe_id*/
            {
                if ((p_cap_ts->lport_or_source == port_id) && (p_cap_ts->seq_id == seq_id)) /*seq_id and source are all match*/
                {
                    p_cap_ts->ts.seconds      = ptp_mac_tx_capture_ts.mac_tx_second;
                    p_cap_ts->ts.nanoseconds  = ptp_mac_tx_capture_ts.mac_tx_ns;
                    is_fifo_match   = TRUE;
                }

                /* add ts to cache*/
                capured_ts.lport_or_source    = port_id;
                capured_ts.seq_id             = seq_id;
                capured_ts.ts.seconds         = ptp_mac_tx_capture_ts.mac_tx_second;
                capured_ts.ts.nanoseconds     = ptp_mac_tx_capture_ts.mac_tx_ns;
                CTC_ERROR_RETURN(_sys_greatbelt_ptp_add_ts_cache(lchip, type, &capured_ts)); /*add ts to cache*/

            }
            sal_memset(&ptp_mac_tx_capture_ts, 0, sizeof(ptp_mac_tx_capture_ts));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_mac_tx_capture_ts));
        }
    }
    else
    {
        return CTC_E_PTP_INVALID_CAPTURED_TYPE;
    }

    /*2. if fail to read ts from FIFO, then read ts from cache*/
    if (!is_fifo_match)
    {
        if (_sys_greatbelt_ptp_get_ts_cache(lchip, type, p_cap_ts) != CTC_E_NONE) /*read ts from cache*/
        {
            return CTC_E_PTP_TS_NOT_READY;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_ptp_map_captured_src(ctc_ptp_captured_src_t* ctc_src_type, uint8* sys_src_type, uint8 is_ctc2sys)
{
    if (is_ctc2sys)
    {
        switch (*ctc_src_type)
        {
            case CTC_PTP_TIEM_INTF_GPIO_0:
                *sys_src_type = SYS_PTP_TIEM_INTF_GPIO_0;
                break;

            case CTC_PTP_TIEM_INTF_GPIO_1:
                *sys_src_type = SYS_PTP_TIEM_INTF_GPIO_1;
                break;

            case CTC_PTP_TIEM_INTF_SYNCE_0:
                *sys_src_type = SYS_PTP_TIEM_INTF_SYNCE_0;
                break;

            case CTC_PTP_TIEM_INTF_SYNCE_1:
                *sys_src_type = SYS_PTP_TIEM_INTF_SYNCE_1;
                break;

            case CTC_PTP_TIEM_INTF_SERDES:
                *sys_src_type = SYS_PTP_TIEM_INTF_SERDES;
                break;

            case CTC_PTP_TIEM_INTF_SYNC_PULSE:
                *sys_src_type = SYS_PTP_TIEM_INTF_SYNC_PULSE;
                break;

            case CTC_PTP_TIEM_INTF_TOD_1PPS:
                *sys_src_type = SYS_PTP_TIEM_INTF_TOD_1PPS;
                break;

            default:
                return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        switch (*sys_src_type)
        {
            case SYS_PTP_TIEM_INTF_GPIO_0:
                *ctc_src_type = CTC_PTP_TIEM_INTF_GPIO_0;
                break;

            case SYS_PTP_TIEM_INTF_GPIO_1:
                *ctc_src_type = CTC_PTP_TIEM_INTF_GPIO_1;
                break;

            case SYS_PTP_TIEM_INTF_SYNCE_0:
                *ctc_src_type = CTC_PTP_TIEM_INTF_SYNCE_0;
                break;

            case SYS_PTP_TIEM_INTF_SYNCE_1:
                *ctc_src_type = CTC_PTP_TIEM_INTF_SYNCE_1;
                break;

            case SYS_PTP_TIEM_INTF_SERDES:
                *ctc_src_type = CTC_PTP_TIEM_INTF_SERDES;
                break;

            case SYS_PTP_TIEM_INTF_SYNC_PULSE:
                *ctc_src_type = CTC_PTP_TIEM_INTF_SYNC_PULSE;
                break;

            case SYS_PTP_TIEM_INTF_TOD_1PPS:
                *ctc_src_type = CTC_PTP_TIEM_INTF_TOD_1PPS;
                break;

            default:
                return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;
}

/**
 @brief  Get timestamp captured by local clock
*/
int32
sys_greatbelt_ptp_get_captured_ts(uint8 lchip, ctc_ptp_capured_ts_t* p_captured_ts)
{
    sys_ptp_capured_ts_t get_ts;
    ctc_ptp_captured_src_t ctc_src = 0;

    sal_memset(&get_ts, 0, sizeof(get_ts));

    /*check*/
    SYS_PTP_INIT_CHECK(lchip);
    CTC_PTR_VALID_CHECK(p_captured_ts);

    /*debug info*/
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_PTP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get captured ts, lchip:%d, type:%d\n", lchip, p_captured_ts->type);

    if (CTC_PTP_CAPTURED_TYPE_RX == p_captured_ts->type) /*read captured rx ts triggered by syncPulse or 1PPS*/
    {
        SYS_PTP_RX_SEQ_ID_CHECK(p_captured_ts->seq_id);
        ctc_src = p_captured_ts->u.captured_src;
        CTC_ERROR_RETURN(_sys_greatbelt_ptp_map_captured_src(&ctc_src, &(get_ts.lport_or_source), 1));

        get_ts.seq_id = p_captured_ts->seq_id;
    }
    else if (CTC_PTP_CAPTURED_TYPE_TX == p_captured_ts->type) /*read captured tx ts triggered by packet transmit*/
    {
        SYS_PTP_LOCAL_PORT_CHECK(p_captured_ts->u.lport);
        SYS_PTP_TX_SEQ_ID_CHECK(p_captured_ts->seq_id);
        get_ts.lport_or_source = p_captured_ts->u.lport;
        get_ts.seq_id = p_captured_ts->seq_id;
    }
    else
    {
        return CTC_E_PTP_INVALID_CAPTURED_TYPE;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_ptp_get_captured_ts(lchip, p_captured_ts->type, &get_ts));
    p_captured_ts->ts.seconds = get_ts.ts.seconds;
    p_captured_ts->ts.nanoseconds = get_ts.ts.nanoseconds;

    return CTC_E_NONE;
}

/**
 @brief  PTP TX Ts capture isr
*/
int32
sys_greatbelt_ptp_isr_tx_ts_capture(uint8 lchip, uint32 intr, void* p_data)
{
    ctc_ptp_ts_cache_t* cache = NULL;
    uint8 gchip = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 port_id = 0;
    uint8 seq_id = 0;
    ptp_mac_tx_capture_ts_t ptp_mac_tx_capture_ts;

        /*check*/
    SYS_PTP_INIT_CHECK(lchip);

    cache = (ctc_ptp_ts_cache_t*)mem_malloc(MEM_PTP_MODULE, sizeof(ctc_ptp_ts_cache_t));
    if (NULL == cache)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(cache, 0, sizeof(ctc_ptp_ts_cache_t));
    sal_memset(&ptp_mac_tx_capture_ts, 0, sizeof(ptp_mac_tx_capture_ts));

    /* read ts from fifo*/
    cache->entry_count = 0;
    cmd = DRV_IOR(PtpMacTxCaptureTs_t, DRV_ENTRY_FLAG);
    CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &ptp_mac_tx_capture_ts), ret, error);

    while ((ptp_mac_tx_capture_ts.mac_tx_bitmap1 != 0) || (ptp_mac_tx_capture_ts.mac_tx_bitmap0 != 0))
    {
        if (CTC_E_NONE == _sys_greatbelt_ptp_get_tx_ts_info(lchip, ptp_mac_tx_capture_ts, &port_id, &seq_id)) /*calculate port_id and sqe_id*/
        {
            cache->entry[cache->entry_count].type = CTC_PTP_CAPTURED_TYPE_TX;
            cache->entry[cache->entry_count].u.lport = port_id;
            cache->entry[cache->entry_count].seq_id = seq_id;
            cache->entry[cache->entry_count].ts.seconds      = ptp_mac_tx_capture_ts.mac_tx_second;
            cache->entry[cache->entry_count].ts.nanoseconds  = ptp_mac_tx_capture_ts.mac_tx_ns;
            cache->entry_count++;
        }
        sal_memset(&ptp_mac_tx_capture_ts, 0, sizeof(ptp_mac_tx_capture_ts));
        CTC_ERROR_GOTO(DRV_IOCTL(lchip, 0, cmd, &ptp_mac_tx_capture_ts), ret, error);
    }

    /* call Users callback*/
    CTC_ERROR_GOTO(sys_greatbelt_get_gchip_id(lchip, &gchip), ret, error);
    if (p_gb_ptp_master[lchip]->g_p_tx_ts_capture_cb)
    {
        ret = p_gb_ptp_master[lchip]->g_p_tx_ts_capture_cb(gchip, cache);
    }

error:
    mem_free(cache);
    return ret;
}

/**
 @brief  PTP RX Ts capture isr
*/
int32
sys_greatbelt_ptp_isr_rx_ts_capture(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 gchip = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 src_bit = 0;
    ptp_captured_adj_frc_t ptp_captured_adj_frc;
    ctc_ptp_time_t captured_ts;
    sys_ptp_capured_ts_t sys_capured_ts;
    ctc_ptp_rx_ts_message_t rx_message;

    SYS_PTP_INIT_CHECK(lchip);

    sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc));
    sal_memset(&captured_ts, 0, sizeof(captured_ts));
    sal_memset(&sys_capured_ts, 0, sizeof(sys_capured_ts));
    sal_memset(&rx_message, 0, sizeof(rx_message));

    /* 1. read captured ts from fifo*/
    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));

    sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
    cmd = DRV_IOR(PtpCapturedAdjFrc_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));

    while (ptp_captured_adj_frc.adj_frc_bitmap) /*the FIFO is empty when the bitmap is 0*/
    {
        for (src_bit = 0; src_bit < 9; src_bit++) /*get captured source from bitmap*/
        {
            if ((ptp_captured_adj_frc.adj_frc_bitmap >> src_bit) & 0x01)
            {
                break;
            }
        }

        if ((src_bit != SYS_PTP_TIEM_INTF_SYNC_PULSE) && (src_bit != SYS_PTP_TIEM_INTF_TOD_1PPS)) /* source match*/
        {
            CTC_ERROR_RETURN(_sys_greatbelt_ptp_map_captured_src(&(rx_message.source), &src_bit, 0));
            rx_message.captured_ts.seconds = ptp_captured_adj_frc.adj_frc_second;
            rx_message.captured_ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;

             /* call Users callback*/
            if (p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb)
            {
                CTC_ERROR_RETURN(p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb(gchip, &rx_message));
            }
        }
        else  /* not mactch source, add ts to cache*/
        {
            sys_capured_ts.lport_or_source = src_bit;
            sys_capured_ts.seq_id       = 0;
            sys_capured_ts.ts.seconds   = ptp_captured_adj_frc.adj_frc_second;
            sys_capured_ts.ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;
            CTC_ERROR_RETURN(_sys_greatbelt_ptp_add_ts_cache(lchip, CTC_PTP_CAPTURED_TYPE_RX, &sys_capured_ts)); /*add ts to cache*/
        }
        sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));
    }


    return ret;
}

/**
 @brief  PTP TOD 1PPS in isr
*/
int32
sys_greatbelt_ptp_isr_tod_pulse_in(uint8 lchip, uint32 intr, void* p_data)
{
    return CTC_E_NONE;
}

/**
 @brief  PTP TOD code ready isr
*/
int32
sys_greatbelt_ptp_isr_tod_code_rdy(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 gchip = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 is_match = 0;
    uint8 src_bit = 0;
    ctc_ptp_rx_ts_message_t tod_message;
    ptp_tod_input_code_t input_code;
    ptp_captured_adj_frc_t ptp_captured_adj_frc;
    ctc_ptp_time_t offset;
    ctc_ptp_time_t input_ts;
    ctc_ptp_time_t captured_ts;
    sys_ptp_capured_ts_t sys_capured_ts;

    SYS_PTP_INIT_CHECK(lchip);

    sal_memset(&tod_message, 0, sizeof(tod_message));
    sal_memset(&input_code, 0, sizeof(input_code));
    sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc));
    sal_memset(&offset, 0, sizeof(offset));
    sal_memset(&input_ts, 0, sizeof(input_ts));
    sal_memset(&captured_ts, 0, sizeof(captured_ts));
    sal_memset(&sys_capured_ts, 0, sizeof(sys_capured_ts));

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));

    tod_message.source = CTC_PTP_TIEM_INTF_TOD_1PPS;

    /* 1. read input code */
    cmd = DRV_IOR(PtpTodInputCode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &input_code));

    if (1 == input_code.tod_code_in_rdy) /*message has received already*/
    {
        /*get timecode*/
        tod_message.u.tod_intf_message.msg_char0                  = 'C';
        tod_message.u.tod_intf_message.msg_char1                  = 'M';
        tod_message.u.tod_intf_message.msg_class                  = input_code.tod_rcv_msg_byte0;
        tod_message.u.tod_intf_message.msg_id                     = input_code.tod_rcv_msg_byte1;
        tod_message.u.tod_intf_message.msg_length                 = (input_code.tod_rcv_msg_byte2 << 8)
            + (input_code.tod_rcv_msg_byte3);
        tod_message.u.tod_intf_message.gps_second_time_of_week    = (input_code.tod_rcv_msg_byte4 << 24)
            + (input_code.tod_rcv_msg_byte5 << 16)
            + (input_code.tod_rcv_msg_byte6 << 8)
            + (input_code.tod_rcv_msg_byte7);
        tod_message.u.tod_intf_message.gps_week                   = (input_code.tod_rcv_msg_byte12 << 8)
            + (input_code.tod_rcv_msg_byte13);
        tod_message.u.tod_intf_message.leap_second                = (int32)input_code.tod_rcv_msg_byte14;
        tod_message.u.tod_intf_message.pps_status                 = input_code.tod_rcv_msg_byte15;
        tod_message.u.tod_intf_message.pps_accuracy               = input_code.tod_rcv_msg_byte16;
        tod_message.u.tod_intf_message.crc                        = input_code.tod_rcv_msg_crc;
        tod_message.u.tod_intf_message.crc_error                  = input_code.tod_rcv_msg_crc_err;


        input_ts.seconds = tod_message.u.tod_intf_message.gps_week * SYS_SECONDS_OF_EACH_WEEK + SYS_TAI_TO_GPS_SECONDS
            + tod_message.u.tod_intf_message.gps_second_time_of_week;

        /* 2. read captured ts from fifo*/
        is_match = 0;
        sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
        cmd = DRV_IOR(PtpCapturedAdjFrc_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));

        while (ptp_captured_adj_frc.adj_frc_bitmap) /*the FIFO is empty when the bitmap is 0*/
        {
            for (src_bit = 0; src_bit < 9; src_bit++) /*get captured source from bitmap*/
            {
                if ((ptp_captured_adj_frc.adj_frc_bitmap >> src_bit) & 0x01)
                {
                    break;
                }
            }

            if ((src_bit == SYS_PTP_TIEM_INTF_TOD_1PPS) && (!is_match)) /* source match*/
            {
                captured_ts.seconds      = ptp_captured_adj_frc.adj_frc_second;
                captured_ts.nanoseconds  = ptp_captured_adj_frc.adj_frc_ns;
                tod_message.captured_ts.seconds = captured_ts.seconds;
                tod_message.captured_ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;
                is_match = 1;
            }
            else if (src_bit != SYS_PTP_TIEM_INTF_SYNC_PULSE)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_ptp_map_captured_src(&(tod_message.source), &src_bit, 0));
                tod_message.captured_ts.seconds = ptp_captured_adj_frc.adj_frc_second;
                tod_message.captured_ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;

                if (p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb)
                {
                   CTC_ERROR_RETURN(p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb(gchip, &tod_message));
                }
            }
            else
            {
                sys_capured_ts.lport_or_source = src_bit;
                sys_capured_ts.seq_id       = 0;
                sys_capured_ts.ts.seconds   = ptp_captured_adj_frc.adj_frc_second;
                sys_capured_ts.ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;
                CTC_ERROR_RETURN(_sys_greatbelt_ptp_add_ts_cache(lchip, CTC_PTP_CAPTURED_TYPE_RX, &sys_capured_ts)); /*add ts to cache*/
            }


            sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));
        }

        if(!is_match)
        {
            sal_memset(&sys_capured_ts, 0, sizeof(sys_capured_ts));
            sys_capured_ts.lport_or_source = SYS_PTP_TIEM_INTF_TOD_1PPS;
            sys_capured_ts.seq_id = 0;

            if (_sys_greatbelt_ptp_get_ts_cache(lchip, CTC_PTP_CAPTURED_TYPE_RX, &sys_capured_ts) != CTC_E_NONE) /*read ts from cache*/
            {
                return CTC_E_PTP_TS_NOT_READY;
            }
            else
            {
                captured_ts.seconds      = sys_capured_ts.ts.seconds;
                captured_ts.nanoseconds  = sys_capured_ts.ts.nanoseconds;
                tod_message.captured_ts.seconds = sys_capured_ts.ts.seconds;
                tod_message.captured_ts.nanoseconds = sys_capured_ts.ts.nanoseconds;
                is_match =1;
            }
        }

        if(is_match)
        {
            /* 3. calculate offset */
            if (p_gb_ptp_master[lchip]->tod_1pps_delay >= SYS_PTP_MAX_TOD_1PPS_DELAY)  /* tod_1pps_delay value bit 31 means delay < 0 */
            {
                if((p_gb_ptp_master[lchip]->tod_1pps_delay -SYS_PTP_MAX_TOD_1PPS_DELAY + captured_ts.nanoseconds) > SYS_PTP_MAX_NS_OR_NNS_VALUE)/*1pps delay between master and slave*/
                {
                    captured_ts.seconds = captured_ts.seconds + 1;
                    captured_ts.nanoseconds = p_gb_ptp_master[lchip]->tod_1pps_delay -SYS_PTP_MAX_TOD_1PPS_DELAY + captured_ts.nanoseconds - SYS_PTP_MAX_NS_OR_NNS_VALUE;
                }
                else
                {
                    captured_ts.nanoseconds = p_gb_ptp_master[lchip]->tod_1pps_delay -SYS_PTP_MAX_TOD_1PPS_DELAY + captured_ts.nanoseconds;
                }
            }
            else
            {
                if(p_gb_ptp_master[lchip]->tod_1pps_delay > captured_ts.nanoseconds)/*1pps delay between master and slave*/
                {
                    captured_ts.seconds = captured_ts.seconds?(captured_ts.seconds - 1):0;
                    captured_ts.nanoseconds = SYS_PTP_MAX_NS_OR_NNS_VALUE + captured_ts.nanoseconds - p_gb_ptp_master[lchip]->tod_1pps_delay;
                }
                else
                {
                    captured_ts.nanoseconds = captured_ts.nanoseconds - p_gb_ptp_master[lchip]->tod_1pps_delay;
                }
            }

            if (input_ts.seconds > captured_ts.seconds)/*TS from ToD is second, no nano second*/
            {
                offset.is_negative = 0;
                offset.seconds = input_ts.seconds - captured_ts.seconds;
                if (0 == captured_ts.nanoseconds)
                {
                    offset.nanoseconds = 0;
                }
                else
                {
                    offset.seconds = offset.seconds?(offset.seconds - 1):0;
                    offset.nanoseconds = SYS_PTP_MAX_NS_OR_NNS_VALUE - captured_ts.nanoseconds;
                }
            }
            else
            {
                offset.is_negative = 1;
                offset.seconds = captured_ts.seconds - input_ts.seconds;
                offset.nanoseconds = captured_ts.nanoseconds;
            }

            /* 4. adjust offset */
            if ((0x01 == tod_message.u.tod_intf_message.msg_class)&&(0x20 == tod_message.u.tod_intf_message.msg_id ))/*time message*/
            {
                 /*CTC_ERROR_RETURN(sys_greatbelt_ptp_adjust_clock_offset(lchip, &offset));*/
                ret = sys_greatbelt_ptp_adjust_clock_offset(lchip, &offset);
                if (CTC_E_NONE != ret)
                {/*if adjust error need clear code rdy*/
                    goto Err;
                }
            }

            /* 5. call Users callback to return code message */
            if (p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb)
            {
                ret = p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb(gchip, &tod_message);
            }
        }



Err:
        /* 6. clear ready flag*/
        input_code.tod_code_in_rdy = 0;
        cmd = DRV_IOW(PtpTodInputCode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &input_code));

    }

    return CTC_E_NONE;
}

/**
 @brief  PTP syncPulse in isr
*/
int32
sys_greatbelt_ptp_isr_sync_pulse_in(uint8 lchip, uint32 intr, void* p_data)
{
    return CTC_E_NONE;
}

/**
 @brief  PTP Sync Interface code ready isr
*/
int32
sys_greatbelt_ptp_isr_sync_code_rdy(uint8 lchip, uint32 intr, void* p_data)
{
    uint8 gchip = 0;
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint8 is_match = 0;
    uint8 src_bit = 0;
    ctc_ptp_rx_ts_message_t sync_message;
    ptp_sync_intf_input_code_t input_code;
    ptp_captured_adj_frc_t ptp_captured_adj_frc;
    ctc_ptp_time_t offset;
    ctc_ptp_time_t input_ts;
    ctc_ptp_time_t captured_ts;
    sys_ptp_capured_ts_t sys_capured_ts;
    uint64 input_nanoseconds = 0;
    uint64 captured_nanoseconds = 0;
    uint64 offset_nanoseconds = 0;

    SYS_PTP_INIT_CHECK(lchip);

    sal_memset(&sync_message, 0, sizeof(sync_message));
    sal_memset(&input_code, 0, sizeof(input_code));
    sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc));
    sal_memset(&offset, 0, sizeof(offset));
    sal_memset(&input_ts, 0, sizeof(input_ts));
    sal_memset(&captured_ts, 0, sizeof(captured_ts));
    sal_memset(&sys_capured_ts, 0, sizeof(sys_capured_ts));

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));

    sync_message.source = CTC_PTP_TIEM_INTF_SYNC_PULSE;

    /* 1. read input code */
    cmd = DRV_IOR(PtpSyncIntfInputCode_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &input_code));
    if (1 == input_code.sync_code_in_rdy) /*timecode has received already*/
    {
        /*get timecode*/
        sync_message.u.sync_intf_message.seconds      = (input_code.data88_to64 & 0xffffff) << 24
            | (input_code.data63_to32 & 0xffffff00) >> 8;
        sync_message.u.sync_intf_message.nanoseconds  = (input_code.data63_to32 & 0xff) << 24
            | (input_code.data31_to0 & 0xffffff00) >> 8;
        sync_message.u.sync_intf_message.lock         = input_code.data88_to64 >> 24;
        sync_message.u.sync_intf_message.accuracy     = input_code.data31_to0 & 0xff;
        sync_message.u.sync_intf_message.crc_error    = input_code.crc_err;
        sync_message.u.sync_intf_message.crc          = input_code.crc;

        input_ts.seconds = sync_message.u.sync_intf_message.seconds;
        input_ts.nanoseconds = sync_message.u.sync_intf_message.nanoseconds;
        /* 2. read captured ts from fifo*/
        is_match = 0;
        sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
        cmd = DRV_IOR(PtpCapturedAdjFrc_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));

        while (ptp_captured_adj_frc.adj_frc_bitmap) /*the FIFO is empty when the bitmap is 0*/
        {
            for (src_bit = 0; src_bit < 9; src_bit++) /*get captured source from bitmap*/
            {
                if ((ptp_captured_adj_frc.adj_frc_bitmap >> src_bit) & 0x01)
                {
                    break;
                }
            }

            if ((src_bit == SYS_PTP_TIEM_INTF_SYNC_PULSE) && (!is_match)) /* source match*/
            {
                captured_ts.seconds      = ptp_captured_adj_frc.adj_frc_second;
                captured_ts.nanoseconds  = ptp_captured_adj_frc.adj_frc_ns;
                sync_message.captured_ts.seconds = captured_ts.seconds;
                sync_message.captured_ts.nanoseconds = captured_ts.nanoseconds;
                is_match = 1;
            }
            else if (src_bit != SYS_PTP_TIEM_INTF_TOD_1PPS)
            {
                CTC_ERROR_RETURN(_sys_greatbelt_ptp_map_captured_src(&(sync_message.source), &src_bit, 0));
                sync_message.captured_ts.seconds = ptp_captured_adj_frc.adj_frc_second;
                sync_message.captured_ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;

                if (p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb)
                {
                   CTC_ERROR_RETURN(p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb(gchip, &sync_message));
                }
            }
            else
            {
                sys_capured_ts.lport_or_source = src_bit;
                sys_capured_ts.seq_id       = 0;
                sys_capured_ts.ts.seconds   = ptp_captured_adj_frc.adj_frc_second;
                sys_capured_ts.ts.nanoseconds = ptp_captured_adj_frc.adj_frc_ns;
                CTC_ERROR_RETURN(_sys_greatbelt_ptp_add_ts_cache(lchip, CTC_PTP_CAPTURED_TYPE_RX, &sys_capured_ts)); /*add ts to cache*/
            }

            sal_memset(&ptp_captured_adj_frc, 0, sizeof(ptp_captured_adj_frc_t));
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ptp_captured_adj_frc));
        }

        if(!is_match)
        {
            sal_memset(&sys_capured_ts, 0, sizeof(sys_capured_ts));
            sys_capured_ts.lport_or_source = SYS_PTP_TIEM_INTF_SYNC_PULSE;
            sys_capured_ts.seq_id = 0;

            if (_sys_greatbelt_ptp_get_ts_cache(lchip, CTC_PTP_CAPTURED_TYPE_RX, &sys_capured_ts) != CTC_E_NONE) /*read ts from cache*/
            {
                return CTC_E_PTP_TS_NOT_READY;
            }
            else
            {
                captured_ts.seconds      = sys_capured_ts.ts.seconds;
                captured_ts.nanoseconds  = sys_capured_ts.ts.nanoseconds;
                sync_message.captured_ts.seconds = sys_capured_ts.ts.seconds;
                sync_message.captured_ts.nanoseconds = sys_capured_ts.ts.nanoseconds;
                is_match =1;
            }
        }

        if(is_match)
        {
            /* 3. calculate offset */
            input_nanoseconds = ((uint64)input_ts.seconds) * SYS_PTP_MAX_NS_OR_NNS_VALUE + input_ts.nanoseconds;
            captured_nanoseconds = ((uint64)captured_ts.seconds) * SYS_PTP_MAX_NS_OR_NNS_VALUE + captured_ts.nanoseconds;
            if (input_nanoseconds >= captured_nanoseconds)
            {
                offset.is_negative = 0;
                offset_nanoseconds = input_nanoseconds - captured_nanoseconds;
            }
            else
            {
                offset.is_negative = 1;
                offset_nanoseconds = captured_nanoseconds - input_nanoseconds;
            }

            offset.seconds = offset_nanoseconds / SYS_PTP_MAX_NS_OR_NNS_VALUE;
            offset.nanoseconds = offset_nanoseconds % SYS_PTP_MAX_NS_OR_NNS_VALUE;
            /* 4. adjust offset */
            ret = sys_greatbelt_ptp_adjust_clock_offset(lchip, &offset);
            if (CTC_E_NONE != ret )
            {
                goto Err;
            }

            /* 5. call Users callback to return code message */
            if (p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb)
            {
                ret = p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb(gchip, &sync_message);
            }
        }
Err:
        /* 6. clear ready flag*/
        input_code.sync_code_in_rdy = 0;
        cmd = DRV_IOW(PtpSyncIntfInputCode_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &input_code));
    }
    return CTC_E_NONE;
}

/**
 @brief  Register TX ts captured callback
*/
int32
sys_greatbelt_ptp_set_tx_ts_captured_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    p_gb_ptp_master[lchip]->g_p_tx_ts_capture_cb = cb;
    return CTC_E_NONE;
}

/**
 @brief  Register input code ready callback
*/
int32
sys_greatbelt_ptp_set_input_code_ready_cb(uint8 lchip, CTC_INTERRUPT_EVENT_FUNC cb)
{
    p_gb_ptp_master[lchip]->g_p_tx_input_ready_cb = cb;
    return CTC_E_NONE;
}

