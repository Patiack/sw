/**
 @file sys_goldengate_sync_ether.c

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
#include "ctc_warmboot.h"
#include "goldengate/include/drv_enum.h"
#include "goldengate/include/drv_io.h"
#include "goldengate/include/drv_common.h"
#include "sys_goldengate_chip.h"
#include "sys_goldengate_sync_ether.h"
#include "sys_goldengate_register.h"
#include "sys_goldengate_wb_common.h"
/* #include "drv_data_path.h" --never--*/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
#define SYS_SYNC_ETHER_CLOCK_MAX 2
#define SYS_SYNC_ETHER_DIVIDER_MAX 32

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
    if (p_gg_sync_ether_master[lchip]->p_sync_ether_mutex) sal_mutex_lock(p_gg_sync_ether_master[lchip]->p_sync_ether_mutex)
#define SYNCE_UNLOCK \
    if (p_gg_sync_ether_master[lchip]->p_sync_ether_mutex) sal_mutex_unlock(p_gg_sync_ether_master[lchip]->p_sync_ether_mutex)

#define SYS_SYNC_ETHER_INIT_CHECK() \
    do { \
        SYS_LCHIP_CHECK_ACTIVE(lchip); \
        if (NULL == p_gg_sync_ether_master[lchip]) \
        { \
            return CTC_E_SYNCE_NOT_INIT; \
        } \
    } while (0)

#define SYS_SYNC_ETHER_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(sync_ether, sync_ether, SYNC_ETHER_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

sys_sync_ether_master_t* p_gg_sync_ether_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};
extern sys_chip_master_t* p_gg_chip_master;
/***************************************************************
 *
 *  Functions
 *
 ***************************************************************/
int32
sys_goldengate_sync_ether_wb_sync(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_data_t wb_data;
    sys_wb_syncether_master_t *pdata;
    uint8 sync_ether_clock_id = 0;

    if (NULL == p_gg_sync_ether_master[lchip])
    {
        return CTC_E_NONE;
    }

    /*syncup buffer*/
    wb_data.buffer = mem_malloc(MEM_SYNC_ETHER_MODULE, CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_data.buffer)
    {
        ret = CTC_E_NO_MEMORY;
        goto done;
    }
    sal_memset(wb_data.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    /*syncup statsid*/
    CTC_WB_INIT_DATA_T((&wb_data), sys_wb_syncether_master_t, CTC_FEATURE_SYNC_ETHER, SYS_WB_APPID_SYNCETHER_SUBID_MASTER);


    pdata = (sys_wb_syncether_master_t *)wb_data.buffer;

    pdata->lchip = lchip;

    for (sync_ether_clock_id = 0; sync_ether_clock_id < SYS_SYNC_ETHER_CLOCK_MAX; sync_ether_clock_id++)
    {
        if (sync_ether_clock_id >= SYS_WB_SYNCETHER_CLOCK_MAX)
        {
            break;
        }

        pdata->recovered_clock_lport[sync_ether_clock_id] = p_gg_sync_ether_master[lchip]->recovered_clock_lport[sync_ether_clock_id];
    }

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
sys_goldengate_sync_ether_wb_restore(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_wb_query_t wb_query;
    uint16 entry_cnt = 0;
    sys_wb_syncether_master_t* pdata = NULL;
    uint8 sync_ether_clock_id = 0;

    sal_memset(&wb_query, 0, sizeof(ctc_wb_query_t));
    wb_query.buffer = mem_malloc(MEM_SYNC_ETHER_MODULE,  CTC_WB_DATA_BUFFER_LENGTH);
    if (NULL == wb_query.buffer)
    {
        CTC_ERROR_GOTO(CTC_E_NO_MEMORY, ret, done);
    }
    sal_memset(wb_query.buffer, 0, CTC_WB_DATA_BUFFER_LENGTH);

    CTC_WB_INIT_QUERY_T((&wb_query), sys_wb_syncether_master_t, CTC_FEATURE_SYNC_ETHER, SYS_WB_APPID_SYNCETHER_SUBID_MASTER);

    CTC_WB_QUERY_ENTRY_BEGIN((&wb_query));
    {
        pdata = (sys_wb_syncether_master_t *)wb_query.buffer + entry_cnt;

        for (sync_ether_clock_id = 0; sync_ether_clock_id < SYS_WB_SYNCETHER_CLOCK_MAX; sync_ether_clock_id++)
        {
            if (sync_ether_clock_id >= SYS_SYNC_ETHER_CLOCK_MAX)
            {
                break;
            }

            p_gg_sync_ether_master[lchip]->recovered_clock_lport[sync_ether_clock_id] = pdata->recovered_clock_lport[sync_ether_clock_id];
        }

        entry_cnt++;
    }
    CTC_WB_QUERY_ENTRY_END((&wb_query));

done:
    if (wb_query.key)
    {
        mem_free(wb_query.key);
    }

    return ret;
}

int32
sys_goldengate_sync_ether_init(uint8 lchip)
{

    int32 ret = CTC_E_NONE;
    uint32 cmd0 = 0;
    uint32 cmd1 = 0;
    uint32 cmd2 = 0;
    uint32 cmd3 = 0;
    uint32 cmd = 0;
    uint32 field_value = 0;
    RlmHsCfgSyncE0_m hs_sync_ether_clk_cfg0;
    RlmHsCfgSyncE0_m hs_sync_ether_clk_cfg1;
    RlmHsCfgSyncE0_m cs_sync_ether_clk_cfg0;
    RlmHsCfgSyncE0_m cs_sync_ether_clk_cfg1;

    if (p_gg_sync_ether_master[lchip] != NULL)
    {
        return CTC_E_NONE;
    }

    /* create SyncE master */
    p_gg_sync_ether_master[lchip] = (sys_sync_ether_master_t*)mem_malloc(MEM_SYNC_ETHER_MODULE, sizeof(sys_sync_ether_master_t));
    if (NULL == p_gg_sync_ether_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memset(p_gg_sync_ether_master[lchip], 0, sizeof(sys_sync_ether_master_t));
    ret = sal_mutex_create(&(p_gg_sync_ether_master[lchip]->p_sync_ether_mutex));
    if (ret || !(p_gg_sync_ether_master[lchip]->p_sync_ether_mutex))
    {
        mem_free(p_gg_sync_ether_master[lchip]);
        return CTC_E_FAIL_CREATE_MUTEX;
    }

    sal_memset(&hs_sync_ether_clk_cfg0, 0, sizeof(hs_sync_ether_clk_cfg0));
    sal_memset(&hs_sync_ether_clk_cfg1, 0, sizeof(hs_sync_ether_clk_cfg1));
    sal_memset(&cs_sync_ether_clk_cfg0, 0, sizeof(cs_sync_ether_clk_cfg0));
    sal_memset(&cs_sync_ether_clk_cfg1, 0, sizeof(cs_sync_ether_clk_cfg1));

    /*set default: link status detect enable, clock divider by 1*/
    SetRlmHsCfgSyncE0(V, cfgClkDividerEtherClkPrimary_f, &hs_sync_ether_clk_cfg0, 0);
    SetRlmHsCfgSyncE0(V, cfgFastLinkFailureDisPrimary_f, &hs_sync_ether_clk_cfg0, 0);
    SetRlmHsCfgSyncE0(V, cfgClkDividerEtherClkSecondary_f, &hs_sync_ether_clk_cfg0, 0);
    SetRlmHsCfgSyncE0(V, cfgFastLinkFailureDisSecondary_f, &hs_sync_ether_clk_cfg0, 0);

    SetRlmHsCfgSyncE1(V, cfgClkDividerEtherClkPrimary_f, &hs_sync_ether_clk_cfg1, 0);
    SetRlmHsCfgSyncE1(V, cfgFastLinkFailureDisPrimary_f, &hs_sync_ether_clk_cfg1, 0);
    SetRlmHsCfgSyncE1(V, cfgClkDividerEtherClkSecondary_f, &hs_sync_ether_clk_cfg1, 0);
    SetRlmHsCfgSyncE1(V, cfgFastLinkFailureDisSecondary_f, &hs_sync_ether_clk_cfg1, 0);

    SetRlmCsCfgSyncE0(V, cfgClkDividerEtherClkPrimary_f, &cs_sync_ether_clk_cfg0, 0);
    SetRlmCsCfgSyncE0(V, cfgFastLinkFailureDisPrimary_f, &cs_sync_ether_clk_cfg0, 0);
    SetRlmCsCfgSyncE0(V, cfgClkDividerEtherClkSecondary_f, &cs_sync_ether_clk_cfg0, 0);
    SetRlmCsCfgSyncE0(V, cfgFastLinkFailureDisSecondary_f, &cs_sync_ether_clk_cfg0, 0);

    SetRlmCsCfgSyncE1(V, cfgClkDividerEtherClkPrimary_f, &cs_sync_ether_clk_cfg1, 0);
    SetRlmCsCfgSyncE1(V, cfgFastLinkFailureDisPrimary_f, &cs_sync_ether_clk_cfg1, 0);
    SetRlmCsCfgSyncE1(V, cfgClkDividerEtherClkSecondary_f, &cs_sync_ether_clk_cfg1, 0);
    SetRlmCsCfgSyncE1(V, cfgFastLinkFailureDisSecondary_f, &cs_sync_ether_clk_cfg1, 0);

    cmd0 = DRV_IOW(RlmHsCfgSyncE0_t, DRV_ENTRY_FLAG);
    cmd1 = DRV_IOW(RlmHsCfgSyncE1_t, DRV_ENTRY_FLAG);
    cmd2 = DRV_IOW(RlmCsCfgSyncE0_t, DRV_ENTRY_FLAG);
    cmd3 = DRV_IOW(RlmCsCfgSyncE1_t, DRV_ENTRY_FLAG);

    if(TRUE)
    {
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd0, &hs_sync_ether_clk_cfg0));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd1, &hs_sync_ether_clk_cfg1));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd2, &cs_sync_ether_clk_cfg0));
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd3, &cs_sync_ether_clk_cfg1));
    }

    /*init cfgTsUseIntRefClk */
    field_value = 0;
    cmd = DRV_IOW(SupMiscCfg_t, SupMiscCfg_cfgTsUseIntRefClk_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    CTC_ERROR_RETURN(sys_goldengate_wb_sync_register_cb(lchip, CTC_FEATURE_SYNC_ETHER, sys_goldengate_sync_ether_wb_sync));

    /* warmboot data restore */
    if (CTC_WB_ENABLE && (CTC_WB_STATUS(lchip) == CTC_WB_STATUS_RELOADING))
    {
        CTC_ERROR_RETURN(sys_goldengate_sync_ether_wb_restore(lchip));
    }

    return CTC_E_NONE;
}

int32
sys_goldengate_sync_ether_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);
    if (NULL == p_gg_sync_ether_master[lchip])
    {
        return CTC_E_NONE;
    }

    sal_mutex_destroy(p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);
    mem_free(p_gg_sync_ether_master[lchip]);

    return CTC_E_NONE;
}

int32
sys_goldengate_sync_ether_set_cfg(uint8 lchip, uint8 sync_ether_clock_id, ctc_sync_ether_cfg_t* p_sync_ether_cfg)
{
    uint32 cmd = 0;
    uint32 cfg = 0;
    uint32 reset_offset = 0;
    uint32 divider_offset = 0;
    uint32 link_status_disable_offset = 0;
    uint32 clock_select_offset = 0;
    uint32 link_select_offset = 0;
    uint32 use_cg_offset = 0;
    uint32 value = 0;
    uint16 serdes_id = 0;
    uint16 chip_port = 0;
    uint8 slice_id = 0;
    uint8 num = 0;
    uint8 clock_select = 0;
    uint8 use_cg_en = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;


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

    if (p_sync_ether_cfg->divider >= SYS_SYNC_ETHER_DIVIDER_MAX)
    {
        return CTC_E_SYNCE_DIVIDER_EXCEED_MAX_VALUE;
    }


    SYNCE_LOCK;
    CTC_ERROR_RETURN_WITH_UNLOCK((sys_goldengate_common_get_port_capability(lchip, p_sync_ether_cfg->recovered_clock_lport, &port_attr)),
                                 p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);

    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        SYNCE_UNLOCK;
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        slice_id = port_attr->slice_id;
        chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_sync_ether_cfg->recovered_clock_lport) - slice_id*256;
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &num),  p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);
    }


    if (0 == slice_id)
    {
        if (serdes_id < 32)
        {
            /*is_hs = 1;*/
            cfg = RlmHsCfgSyncE0_t;
            reset_offset = RlmHsCfgSyncE0_cfgClkResetEtherClkPrimary_f + sync_ether_clock_id;
            divider_offset = RlmHsCfgSyncE0_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmHsCfgSyncE0_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmHsCfgSyncE0_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
            link_select_offset = RlmHsCfgSyncE0_cfgLinkStatusSelectPrimary_f + sync_ether_clock_id;
            clock_select = serdes_id;
        }
        else
        {
            cfg = RlmCsCfgSyncE0_t;
            reset_offset = RlmCsCfgSyncE0_cfgClkResetEtherClkPrimary_f + sync_ether_clock_id;
            divider_offset = RlmCsCfgSyncE0_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmCsCfgSyncE0_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmCsCfgSyncE0_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
            link_select_offset = RlmCsCfgSyncE0_cfgLinkStatusSelectPrimary_f + sync_ether_clock_id;
            use_cg_offset = RlmCsCfgSyncE0_cfgUseCgPrimary_f + sync_ether_clock_id;
            use_cg_en = 1;
            if (serdes_id < 36)
            {
                clock_select = serdes_id -32;
            }
            else if (serdes_id < 40)
            {
                clock_select = serdes_id -28;
            }
            else if (serdes_id < 44)
            {
                clock_select = serdes_id -36;
            }
            else if (serdes_id < 48)
            {
                clock_select = serdes_id -32;
            }
        }
    }
    else
    {
        if (serdes_id < 80)
        {
            /*is_hs = 1;*/
            cfg = RlmHsCfgSyncE1_t;
            reset_offset = RlmHsCfgSyncE1_cfgClkResetEtherClkPrimary_f + sync_ether_clock_id;
            divider_offset = RlmHsCfgSyncE1_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmHsCfgSyncE1_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmHsCfgSyncE1_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
            link_select_offset = RlmHsCfgSyncE1_cfgLinkStatusSelectPrimary_f + sync_ether_clock_id;
            clock_select = serdes_id - 48;
        }
        else
        {
            cfg = RlmCsCfgSyncE1_t;
            reset_offset = RlmCsCfgSyncE1_cfgClkResetEtherClkPrimary_f + sync_ether_clock_id;
            divider_offset = RlmCsCfgSyncE1_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmCsCfgSyncE1_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmCsCfgSyncE1_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
            link_select_offset = RlmCsCfgSyncE1_cfgLinkStatusSelectPrimary_f + sync_ether_clock_id;
            use_cg_offset = RlmCsCfgSyncE1_cfgUseCgPrimary_f + sync_ether_clock_id;
            use_cg_en = 1;
            if (serdes_id < 84)
            {
                clock_select = serdes_id -80;
            }
            else if (serdes_id < 88)
            {
                clock_select = serdes_id -76;
            }
            else if (serdes_id < 92)
            {
                clock_select = serdes_id -84;
            }
            else if (serdes_id < 96)
            {
                clock_select = serdes_id -80;
            }
        }
    }

    /* set reset */
    value = 1;
    cmd = DRV_IOW(cfg, reset_offset);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);

    /* set config */
    value = p_sync_ether_cfg->divider;
    cmd = DRV_IOW(cfg, divider_offset);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);

    value = p_sync_ether_cfg->link_status_detect_en ? 0 : 1;
    cmd = DRV_IOW(cfg, link_status_disable_offset);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);


    value = p_sync_ether_cfg->clock_output_en ?  (1 << clock_select) : 0;
    cmd = DRV_IOW(cfg, clock_select_offset);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);

    value = clock_select;
    cmd = DRV_IOW(cfg, link_select_offset);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);

    if (use_cg_en)
    {
        value = 0;
        if (port_attr->pcs_mode == CTC_CHIP_SERDES_CG_MODE && p_sync_ether_cfg->clock_output_en)
        {
            value = (port_attr->mac_id == 36 || port_attr->mac_id == 100)?0x1:0x2;
        }
        cmd = DRV_IOW(cfg, use_cg_offset);
        CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);
    }

    p_gg_sync_ether_master[lchip]->recovered_clock_lport[sync_ether_clock_id] = p_sync_ether_cfg->recovered_clock_lport;

    /* set reset */
    value = 0;
    cmd = DRV_IOW(cfg, reset_offset);
    CTC_ERROR_RETURN_WITH_UNLOCK(DRV_IOCTL(lchip, 0, cmd, &value), p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);
    SYNCE_UNLOCK;

    return CTC_E_NONE;
}

int32
sys_goldengate_sync_ether_get_cfg(uint8 lchip, uint8 sync_ether_clock_id, ctc_sync_ether_cfg_t* p_sync_ether_cfg)
{
    uint32 cmd = 0;
    uint16 chip_port = 0;
    uint8 slice_id = 0;
    uint8 num = 0;
    /*uint8 is_hs = 0;*/
    uint32 value = 0;
    uint16 serdes_id = 0;
    uint32 cfg = 0;
    uint32 divider_offset = 0;
    uint32 link_status_disable_offset = 0;
    uint32 clock_select_offset = 0;
    sys_datapath_lport_attr_t* port_attr = NULL;


    SYS_SYNC_ETHER_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_SYNC_ETHER_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Get sync-ether config, lchip:%d, clock_id:%d\n", lchip, sync_ether_clock_id);

    SYS_SYNC_ETHER_INIT_CHECK();
    CTC_PTR_VALID_CHECK(p_sync_ether_cfg);
    if (sync_ether_clock_id >= SYS_SYNC_ETHER_CLOCK_MAX)
    {
        return CTC_E_SYNCE_CLOCK_ID_EXCEED_MAX_VALUE;
    }

    p_sync_ether_cfg->recovered_clock_lport = p_gg_sync_ether_master[lchip]->recovered_clock_lport[sync_ether_clock_id];


    /*read config*/
    CTC_ERROR_RETURN(sys_goldengate_common_get_port_capability(lchip, p_sync_ether_cfg->recovered_clock_lport, &port_attr));
    if (port_attr->port_type != SYS_DATAPATH_NETWORK_PORT)
    {
        SYNCE_UNLOCK;
        return CTC_E_MAC_NOT_USED;
    }
    else
    {
        slice_id = port_attr->slice_id;
        chip_port = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(p_sync_ether_cfg->recovered_clock_lport) - slice_id*256;
        CTC_ERROR_RETURN_WITH_UNLOCK(sys_goldengate_datapath_get_serdes_with_lport(lchip, chip_port, slice_id, &serdes_id, &num),  p_gg_sync_ether_master[lchip]->p_sync_ether_mutex);
    }


    if (0 == slice_id)
    {
        if (serdes_id < 32)
        {
            /*is_hs = 1;*/
            cfg = RlmHsCfgSyncE0_t;
            divider_offset = RlmHsCfgSyncE0_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmHsCfgSyncE0_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmHsCfgSyncE0_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
        }
        else
        {
            cfg = RlmCsCfgSyncE0_t;
            divider_offset = RlmCsCfgSyncE0_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmCsCfgSyncE0_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmCsCfgSyncE0_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
        }
    }
    else
    {
        if (serdes_id < 80)
        {
            /*is_hs = 1;*/
            cfg = RlmHsCfgSyncE1_t;
            divider_offset = RlmHsCfgSyncE1_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmHsCfgSyncE1_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmHsCfgSyncE1_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
        }
        else
        {
            cfg = RlmCsCfgSyncE1_t;
            divider_offset = RlmCsCfgSyncE1_cfgClkDividerEtherClkPrimary_f + sync_ether_clock_id;
            link_status_disable_offset = RlmCsCfgSyncE1_cfgFastLinkFailureDisPrimary_f + sync_ether_clock_id;
            clock_select_offset = RlmCsCfgSyncE1_cfgEtherClkSelectPrimary_f + sync_ether_clock_id;
        }
    }

    cmd = DRV_IOR(cfg, divider_offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_sync_ether_cfg->divider = value;

    cmd = DRV_IOR(cfg, link_status_disable_offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_sync_ether_cfg->link_status_detect_en = value ? 0 : 1;

    cmd = DRV_IOR(cfg, clock_select_offset);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &value));
    p_sync_ether_cfg->clock_output_en = value ? 1 : 0;

    return CTC_E_NONE;
}

