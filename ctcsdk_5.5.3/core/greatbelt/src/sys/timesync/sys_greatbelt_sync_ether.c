/**
 @file sys_greatbelt_sync_ether.c

 @date 2012-10-18

 @version v2.0

 sync ethernet function of Humber
*/

/***************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_error.h"
#include "ctc_macro.h"
#include "ctc_debug.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_sync_ether.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_data_path.h"
#include "sys_greatbelt_common.h"

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
#define SYS_SYNC_ETHER_CLOCK_MAX 4
#define SYS_SYNC_ETHER_DIVIDER_MAX 64

/**
 @brief  Define sys layer synce global configure data structure
*/
struct sys_sync_ether_master_s
{
    sal_mutex_t* p_sync_ether_mutex;
    uint8 recovered_clock_lport[SYS_SYNC_ETHER_CLOCK_MAX];
};
typedef struct sys_sync_ether_master_s sys_sync_ether_master_t;

#define SYNCE_LOCK \
    if (p_gb_sync_ether_master[lchip]->p_sync_ether_mutex) sal_mutex_lock(p_gb_sync_ether_master[lchip]->p_sync_ether_mutex)
#define SYNCE_UNLOCK \
    if (p_gb_sync_ether_master[lchip]->p_sync_ether_mutex) sal_mutex_unlock(p_gb_sync_ether_master[lchip]->p_sync_ether_mutex)

#define SYS_SYNC_ETHER_INIT_CHECK() \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gb_sync_ether_master[lchip]) \
        { \
            return CTC_E_SYNCE_NOT_INIT; \
        } \
    } while (0)

#define SYS_SYNC_ETHER_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(sync_ether, sync_ether, SYNC_ETHER_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

static sys_sync_ether_master_t* p_gb_sync_ether_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern sys_chip_master_t* p_gb_chip_master;
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/

STATIC int32
_sys_greatbelt_sync_ether_reset(uint8 lchip, uint8 sync_ether_clock_id)
{
    uint32 cmd = 0, tmp = 1;
    uint32 offset = 0;

    offset = SyncEthernetClkCfg_Cfg0EtherReset_f + 12 * sync_ether_clock_id;
    cmd = DRV_IOW(SyncEthernetClkCfg_t, offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_sync_ether_release_reset(uint8 lchip, uint8 sync_ether_clock_id)
{
    uint32 cmd = 0, tmp = 0;
    uint32 offset = 0;

    offset = SyncEthernetClkCfg_Cfg0EtherReset_f + 12 * sync_ether_clock_id;
    cmd = DRV_IOW(SyncEthernetClkCfg_t, offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_sync_ether_serde_select_set(uint8 lchip, uint8 sync_ether_clock_id, uint8 lport)
{
    uint8 serde_id = 0;
    uint32 tmp = 0;
    uint32 offset = 0;
    uint32 cmd = 0;
    sync_ethernet_select_t sync_ether_serde_select;
    sync_ethernet_clk_cfg_t sync_ether_clk_cfg;
    uint8 lane_idx = 0;
    uint8 gchip = 0;
    uint16 gport = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    sal_memset(&sync_ether_serde_select, 0, sizeof(sync_ether_serde_select));
    sal_memset(&sync_ether_clk_cfg, 0, sizeof(sync_ether_clk_cfg));

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    if (!port_cap.valid)
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    serde_id = port_cap.serdes_id;
/*
    if (port_cap.mac_id < SYS_MAX_GMAC_PORT_NUM)
    {
        CTC_ERROR_RETURN(drv_greatbelt_get_gmac_info(lchip, lport, DRV_CHIP_MAC_SERDES_INFO, &serde_id));
    }
    else
    {
        CTC_ERROR_RETURN(drv_greatbelt_get_sgmac_info(lchip, lport-48, DRV_CHIP_MAC_SERDES_INFO, &serde_id));
    }
*/

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serde_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    lane_idx = (serde_id/8)*8 + lane_idx;

    /* select serdes*/
    tmp = 1<< lane_idx;

    offset = SyncEthernetSelect_Cfg0EtherClkSelect_f + sync_ether_clock_id;
    cmd = DRV_IOW(SyncEthernetSelect_t, offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    /* select link which to detect*/
     /*tmp = lport;*/
    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);
    tmp = (uint32)SYS_GET_MAC_ID(lchip, gport);
    offset = SyncEthernetClkCfg_Cfg0LinkStatusSelect_f + 12 * sync_ether_clock_id;
    cmd = DRV_IOW(SyncEthernetClkCfg_t, offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &tmp));

    /* save info*/
    p_gb_sync_ether_master[lchip]->recovered_clock_lport[sync_ether_clock_id] = lport;

    return CTC_E_NONE;
}

int32
sys_greatbelt_sync_ether_init(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    sync_ethernet_clk_cfg_t sync_ether_clk_cfg;

    if (p_gb_sync_ether_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    /* create SyncE master */
    p_gb_sync_ether_master[lchip] = (sys_sync_ether_master_t*)mem_malloc(MEM_SYNC_ETHER_MODULE, sizeof(sys_sync_ether_master_t));
    if (NULL == p_gb_sync_ether_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gb_sync_ether_master[lchip], 0, sizeof(sys_sync_ether_master_t));
    ret = sal_mutex_create(&(p_gb_sync_ether_master[lchip]->p_sync_ether_mutex));
    if (ret || !(p_gb_sync_ether_master[lchip]->p_sync_ether_mutex))
    {
        mem_free(p_gb_sync_ether_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    sal_memset(&sync_ether_clk_cfg, 0, sizeof(sync_ether_clk_cfg));
    /*set default: clock output disable, link status detect enable, clock divider by 1*/
    sync_ether_clk_cfg.cfg0_ether_user_go = 0;
    sync_ether_clk_cfg.cfg0_ether_divider = 0;
    sync_ether_clk_cfg.cfg0_fast_link_failure_disable = 0;
    sync_ether_clk_cfg.cfg1_ether_user_go = 0;
    sync_ether_clk_cfg.cfg1_ether_divider = 0;
    sync_ether_clk_cfg.cfg1_fast_link_failure_disable = 0;
    sync_ether_clk_cfg.cfg2_ether_user_go = 0;
    sync_ether_clk_cfg.cfg2_ether_divider = 0;
    sync_ether_clk_cfg.cfg2_fast_link_failure_disable = 0;
    sync_ether_clk_cfg.cfg3_ether_user_go = 0;
    sync_ether_clk_cfg.cfg3_ether_divider = 0;
    sync_ether_clk_cfg.cfg3_fast_link_failure_disable = 0;

    cmd = DRV_IOW(SyncEthernetClkCfg_t, DRV_ENTRY_FLAG);

    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sync_ether_clk_cfg));

    return CTC_E_NONE;
}

int32
sys_greatbelt_sync_ether_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gb_sync_ether_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gb_sync_ether_master[lchip]->p_sync_ether_mutex);
    mem_free(p_gb_sync_ether_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_greatbelt_sync_ether_set_cfg(uint8 lchip, uint8 sync_ether_clock_id, ctc_sync_ether_cfg_t* p_sync_ether_cfg)
{
    uint32 cmd = 0;
    sync_ethernet_clk_cfg_t sync_ether_clk_cfg;

    sal_memset(&sync_ether_clk_cfg, 0, sizeof(sync_ether_clk_cfg));

    SYS_SYNC_ETHER_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_sync_ether_cfg);

    /*debug info*/
    SYS_SYNC_ETHER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SYNC_ETHER_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set sync-ether config, lchip:%d, clock_id:%d, divider:%d,\
                  link_status_detect_en:%d, clock_output_en:%d, recovered_clock_lport: %d\n", lchip,
                           sync_ether_clock_id, p_sync_ether_cfg->divider, p_sync_ether_cfg->link_status_detect_en,
                           p_sync_ether_cfg->clock_output_en, p_sync_ether_cfg->recovered_clock_lport);


    if (sync_ether_clock_id >= SYS_SYNC_ETHER_CLOCK_MAX)
    {
        return CTC_E_SYNCE_CLOCK_ID_EXCEED_MAX_VALUE;
    }

    if (p_sync_ether_cfg->recovered_clock_lport >= SYS_GB_MAX_PHY_PORT)
    {
        return CTC_E_LOCAL_PORT_NOT_EXIST;
    }

    if (p_sync_ether_cfg->divider >= SYS_SYNC_ETHER_DIVIDER_MAX)
    {
        return CTC_E_SYNCE_DIVIDER_EXCEED_MAX_VALUE;
    }

    SYNCE_LOCK;
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_sync_ether_reset(lchip, sync_ether_clock_id),
                                 p_gb_sync_ether_master[lchip]->p_sync_ether_mutex);
    /* set config*/
    cmd = DRV_IOR(SyncEthernetClkCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &sync_ether_clk_cfg), p_gb_sync_ether_master[lchip]->p_sync_ether_mutex);

    switch (sync_ether_clock_id)
    {
    case 0:
        sync_ether_clk_cfg.cfg0_ether_user_go = p_sync_ether_cfg->clock_output_en ? 1 : 0;
        sync_ether_clk_cfg.cfg0_ether_divider = p_sync_ether_cfg->divider;
        sync_ether_clk_cfg.cfg0_fast_link_failure_disable = p_sync_ether_cfg->link_status_detect_en ? 0 : 1;
        break;

    case 1:
        sync_ether_clk_cfg.cfg1_ether_user_go = p_sync_ether_cfg->clock_output_en ? 1 : 0;
        sync_ether_clk_cfg.cfg1_ether_divider = p_sync_ether_cfg->divider;
        sync_ether_clk_cfg.cfg1_fast_link_failure_disable = p_sync_ether_cfg->link_status_detect_en ? 0 : 1;
        break;

    case 2:
        sync_ether_clk_cfg.cfg2_ether_user_go = p_sync_ether_cfg->clock_output_en ? 1 : 0;
        sync_ether_clk_cfg.cfg2_ether_divider = p_sync_ether_cfg->divider;
        sync_ether_clk_cfg.cfg2_fast_link_failure_disable = p_sync_ether_cfg->link_status_detect_en ? 0 : 1;
        break;

    case 3:
        sync_ether_clk_cfg.cfg3_ether_user_go = p_sync_ether_cfg->clock_output_en ? 1 : 0;
        sync_ether_clk_cfg.cfg3_ether_divider = p_sync_ether_cfg->divider;
        sync_ether_clk_cfg.cfg3_fast_link_failure_disable = p_sync_ether_cfg->link_status_detect_en ? 0 : 1;
        break;

    default:
        SYNCE_UNLOCK;
        return CTC_E_INVALID_PARAM;
    }

    cmd = DRV_IOW(SyncEthernetClkCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &sync_ether_clk_cfg), p_gb_sync_ether_master[lchip]->p_sync_ether_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_sync_ether_serde_select_set(lchip, sync_ether_clock_id,
                                                                            p_sync_ether_cfg->recovered_clock_lport), p_gb_sync_ether_master[lchip]->p_sync_ether_mutex);
    CTC_ERROR_RETURN_WITH_UNLOCK(_sys_greatbelt_sync_ether_release_reset(lchip, sync_ether_clock_id),
                                 p_gb_sync_ether_master[lchip]->p_sync_ether_mutex);
    SYNCE_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_greatbelt_sync_ether_get_cfg(uint8 lchip, uint8 sync_ether_clock_id, ctc_sync_ether_cfg_t* p_sync_ether_cfg)
{
    uint32 cmd = 0;
    sync_ethernet_clk_cfg_t sync_ether_clk_cfg;

    sal_memset(&sync_ether_clk_cfg, 0, sizeof(sync_ether_clk_cfg));

    SYS_SYNC_ETHER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SYNC_ETHER_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get sync-ether config, lchip:%d, clock_id:%d\n", lchip, sync_ether_clock_id);

    SYS_SYNC_ETHER_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_sync_ether_cfg);
    if (sync_ether_clock_id >= SYS_SYNC_ETHER_CLOCK_MAX)
    {
        return CTC_E_SYNCE_CLOCK_ID_EXCEED_MAX_VALUE;
    }

    /*read config*/
    cmd = DRV_IOR(SyncEthernetClkCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sync_ether_clk_cfg));

    switch (sync_ether_clock_id)
    {
    case 0:
        p_sync_ether_cfg->clock_output_en = sync_ether_clk_cfg.cfg0_ether_user_go ? 1 : 0;
        p_sync_ether_cfg->divider = sync_ether_clk_cfg.cfg0_ether_divider;
        p_sync_ether_cfg->link_status_detect_en = sync_ether_clk_cfg.cfg0_fast_link_failure_disable ? 0 : 1;
        break;

    case 1:
        p_sync_ether_cfg->clock_output_en = sync_ether_clk_cfg.cfg1_ether_user_go ? 1 : 0;
        p_sync_ether_cfg->divider = sync_ether_clk_cfg.cfg1_ether_divider;
        p_sync_ether_cfg->link_status_detect_en = sync_ether_clk_cfg.cfg1_fast_link_failure_disable ? 0 : 1;
        break;

    case 2:
        p_sync_ether_cfg->clock_output_en = sync_ether_clk_cfg.cfg2_ether_user_go ? 1 : 0;
        p_sync_ether_cfg->divider = sync_ether_clk_cfg.cfg2_ether_divider;
        p_sync_ether_cfg->link_status_detect_en = sync_ether_clk_cfg.cfg2_fast_link_failure_disable ? 0 : 1;
        break;

    case 3:
        p_sync_ether_cfg->clock_output_en = sync_ether_clk_cfg.cfg3_ether_user_go ? 1 : 0;
        p_sync_ether_cfg->divider = sync_ether_clk_cfg.cfg3_ether_divider;
        p_sync_ether_cfg->link_status_detect_en = sync_ether_clk_cfg.cfg3_fast_link_failure_disable ? 0 : 1;
        break;

    default:
        return CTC_E_INVALID_PARAM;
    }

    p_sync_ether_cfg->recovered_clock_lport = p_gb_sync_ether_master[lchip]->recovered_clock_lport[sync_ether_clock_id];

    return CTC_E_NONE;

}

