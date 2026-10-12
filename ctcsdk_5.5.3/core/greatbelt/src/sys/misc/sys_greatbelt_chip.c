/**
 @file sys_greatbelt_chip.c

 @date 2009-10-19

 @version v2.0

 The file define APIs of chip of sys layer
*/
/****************************************************************************
 *
 * Header Files
 *
 ****************************************************************************/
#include "ctc_error.h"
#include "ctc_register.h"
#include "sys_greatbelt_register.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_port.h"
#include "sys_greatbelt_common.h"
#include "sys_greatbelt_ftm.h"
#include "sys_greatbelt_queue_enq.h"

#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_peri_io.h"
#include "greatbelt/include/drv_chip_ctrl.h"
#include "greatbelt/include/drv_data_path.h"
#include "greatbelt/include/drv_chip_info.h"



/* feature support array, 1 means enable, 0 means disable */
uint8 gb_feature_support_arr[SYS_CHIP_TYPE_MAX][CTC_FEATURE_MAX] =
{
/*CHIP_TYPE*/ /*PORT VLAN LINKAGG CHIP FTM NEXTHOP L2 L3IF IPUC IPMC IP_TUNNEL SCL ACL QOS SECURITY STATS MPLS OAM APS PTP DMA INTERRUPT PACKET PDU MIRROR BPE STACKING OVERLAY IPFIX EFD MONITOR FCOE TRILL*/
/* GB     */  { 1,   1,   1,      1,   1,  1,      1,   1,   1,  1,  1,        1,  1,  1,   1,      1,    1,   1,  1,  1,  1,   1,       1,     1,  1,     1,  1,       0,      0,    0,  0,      0,    0  },
/* RAMA   */  { 1,   1,   1,      1,   1,  1,      1,   1,   1,  1,  1,        1,  1,  1,   1,      1,    1,   1,  1,  1,  1,   1,       1,     1,  1,     0,  1,       0,      0,    0,  0,      0,    0  },
/* RIALTO */  { 1,   1,   1,      1,   1,  1,      1,   1,   1,  1,  0,        1,  1,  1,   1,      1,    0,   0,  0,  0,  1,   1,       1,     1,  1,     0,  1,       0,      0,    0,  0,      0,    0  }
};

struct sys_chip_serdes_mac_info_s
{
    char* p_datapath_mode;
    uint8 serdes_id[60];
};
typedef struct sys_chip_serdes_mac_info_s sys_chip_serdes_mac_info_t;

#define DYNAMIC_EDRAM_STATS_INDEX_BASE     224*1024

/****************************************************************************
 *
 * Global and static
 *
 *****************************************************************************/
sys_chip_master_t* p_gb_chip_master[CTC_MAX_LOCAL_CHIP_NUM] = {NULL};

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

/**
 @brief The function is to initialize the chip module and set the local chip number of the linecard
*/
int32
sys_greatbelt_chip_init(uint8 lchip, uint8 lchip_num)
{
    int32 ret = CTC_E_NONE;

    if ((lchip >= CTC_MAX_LOCAL_CHIP_NUM)
        || (lchip_num > SYS_GB_MAX_LOCAL_CHIP_NUM)
        || (lchip_num > CTC_MAX_LOCAL_CHIP_NUM))
    {
        return CTC_E_INVALID_CHIP_NUM;
    }

    if (NULL != p_gb_chip_master[lchip])
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_chip_set_active(lchip, TRUE));

    p_gb_chip_master[lchip] = (sys_chip_master_t*)mem_malloc(MEM_SYSTEM_MODULE, sizeof(sys_chip_master_t));

    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NO_MEMORY;
    }
    g_lchip_num = lchip_num;
    sal_memset(p_gb_chip_master[lchip], 0, sizeof(sys_chip_master_t));

    /* create mutex for chip module */
    ret = sal_mutex_create(&(p_gb_chip_master[lchip]->p_chip_mutex));
    if (ret || (!p_gb_chip_master[lchip]->p_chip_mutex))
    {
        ret = CTC_E_FAIL_CREATE_MUTEX;
        mem_free(p_gb_chip_master[lchip]);
        return ret;
    }

    ret = drv_greatbelt_init(lchip_num, 0);
    if (ret < 0)
    {
        mem_free(p_gb_chip_master[lchip]);
        ret = CTC_E_DRV_FAIL;
    }

    return ret;
}

int32
sys_greatbelt_chip_deinit(uint8 lchip)
{
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_active(lchip, FALSE));

    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NONE;
    }

#if 0
    /*release chip mutex*/
    sal_mutex_destroy(p_gb_chip_master[lchip]->p_chip_mutex);
    mem_free(p_gb_chip_master[lchip]);
#endif

    return CTC_E_NONE;
}

/**
 @brief The function is parse datapath file
*/
int32
sys_greatbelt_parse_datapath_file(uint8 lchip, char* datapath_config_file)
{
    int32 ret = 0;
    drv_work_platform_type_t platform_type;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_parse_datapath_file(datapath_config_file));

    return CTC_E_NONE;
}

/**
 @brief The function is to initialize datapath
*/
int32
sys_greatbelt_init_pll_hss(uint8 lchip)
{
    int32 ret = 0;
    drv_work_platform_type_t platform_type;

    SYS_LCHIP_CHECK_ACTIVE(lchip);
    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_init_pll_hss());


    return CTC_E_NONE;
}

/**
 @brief The function is to initialize datapath
*/
int32
sys_greatbelt_datapath_init(uint8 lchip)
{
    int32 ret         = 0;

    ret = sys_greatbelt_init_pll_hss(lchip);
    if (ret != 0)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP,"sys_greatbelt_init_pll_hss failed:%s@%d %s\n",  __FUNCTION__, __LINE__, ctc_get_error_desc(ret));
        return ret;
    }

    CTC_ERROR_RETURN(drv_greatbelt_init_total());

    return CTC_E_NONE;
}

/**
 @brief The function is to initialize datapath for simulation env
*/
int32
sys_greatbelt_datapath_sim_init(uint8 lchip)
{
    CTC_ERROR_RETURN(drv_greatbelt_datapath_sim_init());

    return CTC_E_NONE;
}

int32
sys_greatbelt_datapath_deinit(uint8 lchip)
{
    LCHIP_CHECK(lchip);

    return CTC_E_NONE;
}

/**
 @brief The function is to initialize the parity ecc error
*/
int32
sys_greatbelt_chip_ecc_recover_init(uint8 lchip)
{
    drv_ecc_recover_global_cfg_t cfg;

    sal_memset(&cfg, 0, sizeof(drv_ecc_recover_global_cfg_t));

    cfg.ftm_check_cb = sys_greatbelt_ftm_check_tbl_recover;
    CTC_ERROR_RETURN(drv_greatbelt_ecc_recover_init(lchip, &cfg));

    return CTC_E_NONE;
}

/**
 @brief The function is to get parity error recover enable
*/
uint32
sys_greatbelt_chip_get_ecc_recover_en(uint8 lchip)
{
    return p_gb_chip_master[lchip]->ecc_recover_en;
}

/**
 @brief The function is to get the local chip num
*/
uint8
sys_greatbelt_get_local_chip_num(uint8 lchip)
{
    return g_lchip_num;
}

/**
 @brief The function is to set chip's global chip id
*/
int32
sys_greatbelt_set_gchip_id(uint8 lchip, uint8 gchip_id)
{
    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    SYS_GLOBAL_CHIPID_CHECK(gchip_id);

    p_gb_chip_master[lchip]->g_chip_id = gchip_id;

    return CTC_E_NONE;
}

/**
 @brief The function is to get chip's global chip id
*/
int32
sys_greatbelt_get_gchip_id(uint8 lchip, uint8* gchip_id)
{
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_PTR_VALID_CHECK(gchip_id);

    *gchip_id = p_gb_chip_master[lchip]->g_chip_id;

    return CTC_E_NONE;
}

/* function that call sys_goldengate_get_local_chip_id needs to check return */
int32
sys_greatbelt_get_local_chip_id(uint8 gchip_id, uint8* lchip_id)
{
    uint8 i = 0;
    uint8 is_find = 0;

    CTC_PTR_VALID_CHECK(lchip_id);

    /* different chips have same lchip num */
    for (i = 0; i < g_lchip_num; i++)
    {
        if ((NULL != p_gb_chip_master[i])
            && (p_gb_chip_master[i]->g_chip_id == gchip_id))
        {
            is_find = 1;
            break;
        }
    }

    if (is_find)
    {
        *lchip_id = i;
        return CTC_E_NONE;
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }
}


/**
 @brief The function is to judge whether the chip is local
*/
bool
sys_greatbelt_chip_is_local(uint8 lchip, uint8 gchip_id)
{
    bool ret        = FALSE;

    if ((NULL != p_gb_chip_master[lchip])
        && (p_gb_chip_master[lchip]->g_chip_id == gchip_id))
    {
                ret     = TRUE;
    }

    return ret;
}

/**
 @brief The function is to set chip's global cfg

*/
int32
sys_greatbelt_set_chip_global_cfg(uint8 lchip, ctc_chip_global_cfg_t* chip_cfg)
{
    uint32 cmd = 0;
    ctc_chip_device_info_t device_info;
    cpu_mac_sa_cfg_t cpu_macsa;
    cpu_mac_da_cfg_t cpu_macda;
    cpu_mac_ctl_t cpu_mac_ctl;
    cpu_mac_misc_ctl_t cpu_mac_misc_ctl;

    if (NULL == p_gb_chip_master[lchip])
    {
        return CTC_E_NOT_INIT;
    }

    CTC_PTR_VALID_CHECK(chip_cfg);

    sal_memcpy(p_gb_chip_master[lchip]->cpu_mac_sa, chip_cfg->cpu_mac_sa, sizeof(chip_cfg->cpu_mac_sa));
    sal_memcpy(p_gb_chip_master[lchip]->cpu_mac_da, chip_cfg->cpu_mac_da, sizeof(chip_cfg->cpu_mac_da));

    p_gb_chip_master[lchip]->cut_through_en       = chip_cfg->cut_through_en;
    p_gb_chip_master[lchip]->cut_through_speed    = chip_cfg->cut_through_speed;
    p_gb_chip_master[lchip]->ecc_recover_en       = chip_cfg->ecc_recover_en;
    p_gb_chip_master[lchip]->cpu_eth_en = chip_cfg->cpu_port_en;

    sal_memset(&cpu_macsa, 0, sizeof(cpu_mac_sa_cfg_t));
    sal_memset(&cpu_macda, 0, sizeof(cpu_mac_da_cfg_t));
    sal_memset(&cpu_mac_ctl, 0, sizeof(cpu_mac_ctl_t));
    sal_memset(&cpu_mac_misc_ctl, 0, sizeof(cpu_mac_misc_ctl_t));
    sal_memset(&device_info, 0, sizeof(ctc_chip_device_info_t));

    cmd = DRV_IOR(CpuMacMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_mac_misc_ctl));

    cmd = DRV_IOR(CpuMacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_mac_ctl));

    cmd = DRV_IOR(CpuMacSaCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_macsa));

    cmd = DRV_IOR(CpuMacDaCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_macda));

    cpu_macsa.cpu_mac_sa31_to0 = chip_cfg->cpu_mac_sa[2] << 24 | chip_cfg->cpu_mac_sa[3] << 16 | chip_cfg->cpu_mac_sa[4] << 8 | chip_cfg->cpu_mac_sa[5];
    cpu_macsa.cpu_mac_sa47_to32 = chip_cfg->cpu_mac_sa[0] << 8 | chip_cfg->cpu_mac_sa[1];

    cpu_macda.cpu_mac0_da31_to0 = chip_cfg->cpu_mac_da[0][2] << 24 | chip_cfg->cpu_mac_da[0][3] << 16 | chip_cfg->cpu_mac_da[0][4] << 8 | chip_cfg->cpu_mac_da[0][5];
    cpu_macda.cpu_mac0_da47_to32 = chip_cfg->cpu_mac_da[0][0] << 8 | chip_cfg->cpu_mac_da[0][1];

    cpu_macda.cpu_mac1_da31_to0 = chip_cfg->cpu_mac_da[1][2] << 24 | chip_cfg->cpu_mac_da[1][3] << 16 | chip_cfg->cpu_mac_da[1][4] << 8 | chip_cfg->cpu_mac_da[1][5];
    cpu_macda.cpu_mac1_da47_to32 = chip_cfg->cpu_mac_da[1][0] << 8 | chip_cfg->cpu_mac_da[1][1];

    cpu_macda.cpu_mac2_da31_to0 = chip_cfg->cpu_mac_da[2][2] << 24 | chip_cfg->cpu_mac_da[2][3] << 16 | chip_cfg->cpu_mac_da[2][4] << 8 | chip_cfg->cpu_mac_da[2][5];
    cpu_macda.cpu_mac2_da47_to32 = chip_cfg->cpu_mac_da[2][0] << 8 | chip_cfg->cpu_mac_da[2][1];

    cpu_macda.cpu_mac3_da31_to0 = chip_cfg->cpu_mac_da[3][2] << 24 | chip_cfg->cpu_mac_da[3][3] << 16 | chip_cfg->cpu_mac_da[3][4] << 8 | chip_cfg->cpu_mac_da[3][5];
    cpu_macda.cpu_mac3_da47_to32 = chip_cfg->cpu_mac_da[3][0] << 8 | chip_cfg->cpu_mac_da[3][1];

    cpu_mac_misc_ctl.egress_add_en       = 1;
    cpu_mac_misc_ctl.ingress_remove_en   = 1;
    cpu_mac_misc_ctl.ipg_cfg             = 3;
    cpu_mac_misc_ctl.en_clk_pcs          = 1;
    cpu_mac_misc_ctl.en_clk_gmac         = 1;
    cpu_mac_misc_ctl.egress_drain_enable = 1;
    cpu_mac_misc_ctl.egress_crc_chk_en   = 0;

    cpu_mac_ctl.oam_cpu_select_en     = 1;
    cpu_mac_ctl.index0                = 0;
    cpu_mac_ctl.index1                = 1;
    cpu_mac_ctl.index2                = 2;
    cpu_mac_ctl.index3                = 3;
    cpu_mac_ctl.index4                = 0;
    cpu_mac_ctl.index5                = 1;
    cpu_mac_ctl.index6                = 2;
    cpu_mac_ctl.index7                = 3;
    cpu_mac_ctl.index8                = 0;
    cpu_mac_ctl.index9                = 1;
    cpu_mac_ctl.index10               = 2;
    cpu_mac_ctl.index11               = 3;
    cpu_mac_ctl.index12               = 0;
    cpu_mac_ctl.index13               = 1;
    cpu_mac_ctl.index14               = 2;
    cpu_mac_ctl.index15               = 3;
    cpu_mac_ctl.nexthop_ptr_bits_type = 0;

    cpu_mac_ctl.ether_type = SYS_CHIP_CPU_MAC_ETHER_TYPE;
    cpu_mac_ctl.vlan_id = chip_cfg->vlanid;
    cpu_mac_ctl.tpid = chip_cfg->tpid;

    cmd = DRV_IOW(CpuMacSaCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_macsa));

    cmd = DRV_IOW(CpuMacDaCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_macda));

    cmd = DRV_IOW(CpuMacCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_mac_ctl));

    cmd = DRV_IOW(CpuMacMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(chip_cfg->lchip, 0, cmd, &cpu_mac_misc_ctl));

    CTC_ERROR_RETURN(sys_greatbelt_chip_get_device_info(lchip, &device_info));

    if ((SYS_CHIP_DEVICE_ID_GB_5160 == device_info.device_id) ||
        (SYS_CHIP_DEVICE_ID_GB_5162 == device_info.device_id) ||
        (SYS_CHIP_DEVICE_ID_GB_5163== device_info.device_id))
    {
        p_gb_chip_master[lchip]->chip_type = SYS_CHIP_TYPE_GREATBELT;
    }
    else if (SYS_CHIP_DEVICE_ID_RM_5120== device_info.device_id)
    {
        p_gb_chip_master[lchip]->chip_type = SYS_CHIP_TYPE_RAMA;
    }
    else if ((SYS_CHIP_DEVICE_ID_RT_3162 == device_info.device_id) ||
        (SYS_CHIP_DEVICE_ID_RT_3163 == device_info.device_id))
    {
        p_gb_chip_master[lchip]->chip_type = SYS_CHIP_TYPE_RIALTO;
    }

    p_gb_chip_master[lchip]->gb_gg_interconnect_en = chip_cfg->gb_gg_interconnect_en;

    return CTC_E_NONE;

}

int32
sys_greatbelt_get_chip_clock(uint8 lchip, uint16* freq)
{
    *freq = sys_greatbelt_get_core_freq(0);

    return CTC_E_NONE;
}

int32
sys_greatbelt_get_chip_cpumac(uint8 lchip, uint8* mac_sa, uint8* mac_da)
{
    sal_memcpy(mac_sa, p_gb_chip_master[lchip]->cpu_mac_sa, sizeof(mac_addr_t));
    sal_memcpy(mac_da, p_gb_chip_master[lchip]->cpu_mac_da[0], sizeof(mac_addr_t));

    return CTC_E_NONE;
}

int32
sys_greatbelt_get_cut_through_en(uint8 lchip)
{
    return p_gb_chip_master[lchip]->cut_through_en;
}

int32
sys_greatbelt_get_cut_through_speed(uint8 lchip, uint16 gport)
{
    int32 speed = 0;
    uint8 lport = 0;

    drv_datapath_port_capability_t port_cap;

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    if(0 == p_gb_chip_master[lchip]->cut_through_speed)
    {
        speed = 0;
    }
    else if(3 == p_gb_chip_master[lchip]->cut_through_speed)
    {
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
        if (DRV_PORT_TYPE_1G == port_cap.port_type)
        {
            speed = 2;
        }
        else  if (DRV_PORT_TYPE_SG == port_cap.port_type)
        {
            speed = 3;
        }

    }
    else
    {
        speed = p_gb_chip_master[lchip]->cut_through_speed + 1;
    }

    return speed;
}

int32
sys_greatbelt_get_gb_gg_interconnect_en(uint8 lchip)
{
    return p_gb_chip_master[lchip]->gb_gg_interconnect_en;
}

int32
sys_greatbelt_get_chip_sensor(uint8 lchip, ctc_chip_sensor_type_t type, uint32* p_value)
{
    gb_sensor_ctl_t ctc_sensor;
    uint32 cmd = 0;
    uint32 field_val = 0;

    sal_memset(&ctc_sensor, 0, sizeof(gb_sensor_ctl_t));

    /* reset sensor */
    field_val = 1;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorReset_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* delay 1ms */
    sal_task_sleep(1);

    /* release reset */
    field_val = 0;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorReset_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* delay 1ms */
    sal_task_sleep(6);

    /* set sensor type*/
    field_val = type;
    cmd = DRV_IOW(GbSensorCtl_t, GbSensorCtl_CfgSensorVolt_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

    /* get sensor value */
    cmd = DRV_IOR(GbSensorCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ctc_sensor));

    if (ctc_sensor.sensor_valid)
    {
        field_val = ctc_sensor.sensor_value;
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Sensor is not Ready!! \n");
        return CTC_E_NONE;
    }

    if (CTC_CHIP_SENSOR_TEMP == type)
    {
        /* Temperature_in_C = [(sensorValue/2) - 90] */
        if ((field_val >> 1) >= 90)
        {
            *p_value = (field_val >> 1) - 90;
        }
        else
        {
            *p_value = 90 - (field_val >> 1) + (1 << 31);
        }
    }
    else
    {
        /* Voltage_in_mV = [(sensorValue) * 1.8617] + 506.1173 */
        *p_value = (field_val*18617)/10000 + 506;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_get_device_info(uint8 lchip, ctc_chip_device_info_t* p_device_info)
{
    uint32 cmd = 0;
    device_id_t device_id;

    CTC_PTR_VALID_CHECK(p_device_info);

    sal_memset(&device_id, 0, sizeof(device_id_t));
    cmd = DRV_IOR(DeviceId_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &device_id));

    switch(device_id.device_id)
    {
        case 0xf5: /*5160*/
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_GB_5160;
            sal_strcpy(p_device_info->chip_name, "CTC5160");
            break;

        case 0x25: /*5120*/
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_RM_5120;
            sal_strcpy(p_device_info->chip_name, "CTC5120");
            break;

        case 0x35: /*5162*/
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_GB_5162;
            sal_strcpy(p_device_info->chip_name, "CTC5162");
            break;
        case 0xb5: /*5163*/
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_GB_5163;
            sal_strcpy(p_device_info->chip_name, "CTC5163");
            break;

        case 0x15: /*3162*/
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_RT_3162;
            sal_strcpy(p_device_info->chip_name, "CTC3162");
            break;

        case 0x95: /*3163*/
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_RT_3163;
            sal_strcpy(p_device_info->chip_name, "CTC3163");
            break;

        default:
            p_device_info->device_id  = SYS_CHIP_DEVICE_ID_INVALID;
            sal_strcpy(p_device_info->chip_name, "Not Support Chip");
            break;
    }
    p_device_info->version_id = device_id.device_rev;

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_select_sgmac_hss(uint8 lchip, uint8 sgmac_id, uint8 hss_id)
{
    /* refer to drv_greatbelt_pll_sgmac_init */
    uint32 cmd = 0;
    int32 ret = CTC_E_NONE;

    sgmac_mode_cfg_t sgmac_mode_cfg;
    sal_memset(&sgmac_mode_cfg, 0, sizeof(sgmac_mode_cfg_t));

    cmd = DRV_IOR(SgmacModeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode_cfg));

    switch(sgmac_id)
    {
        case 6:
            if (2 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac6_sel_hss2 = 1;
            }
            else
            {
                sgmac_mode_cfg.cfg_sgmac6_sel_hss2 = 0;
            }
            break;

        case 7:
            if (2 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac7_sel_hss2 = 1;
            }
            else
            {
                sgmac_mode_cfg.cfg_sgmac7_sel_hss2 = 0;
            }
            break;

        case 8:
            if (2 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac8_sel_hss2 = 1;
            }
            else
            {
                sgmac_mode_cfg.cfg_sgmac8_sel_hss2 = 0;
            }
            break;

        case 9:
            if (2 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac9_sel_hss2 = 1;
            }
            else
            {
                sgmac_mode_cfg.cfg_sgmac9_sel_hss2 = 0;
            }
            break;

        case 10:
            if (2 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac10_sel_hss2 = 1;
                sgmac_mode_cfg.cfg_sgmac10_sel_hss0 = 0;
            }
            else if (0 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac10_sel_hss2 = 0;
                sgmac_mode_cfg.cfg_sgmac10_sel_hss0 = 1;
            }
            else /* 1 */
            {
                sgmac_mode_cfg.cfg_sgmac10_sel_hss2 = 0;
                sgmac_mode_cfg.cfg_sgmac10_sel_hss0 = 0;
            }


            break;
        case 11:
            if (2 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac11_sel_hss2 = 1;
                sgmac_mode_cfg.cfg_sgmac11_sel_hss0 = 0;
            }
            else if (0 == hss_id)
            {
                sgmac_mode_cfg.cfg_sgmac11_sel_hss2 = 0;
                sgmac_mode_cfg.cfg_sgmac11_sel_hss0 = 1;
            }
            else /* 1 */
            {
                sgmac_mode_cfg.cfg_sgmac11_sel_hss2 = 0;
                sgmac_mode_cfg.cfg_sgmac11_sel_hss0 = 0;
            }

            break;

        default:
            ret = CTC_E_NONE;
            break;
    }

    if (ret != CTC_E_NONE)
    {
        return ret;
    }

    cmd = DRV_IOW(SgmacModeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode_cfg));

    return ret;

}


STATIC int32
_sys_greatbelt_chip_set_sgmac_mode_cfg(uint8 lchip, uint8 hssid, uint8 mode)
{
    uint32 tb_id = SgmacModeCfg_t;
    uint32 cmd = 0;
    sgmac_mode_cfg_t sgmac_mode;

    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));

    if (mode == DRV_SERDES_XAUI_MODE)
    {
        /* Config SgmacModeCfg */
        if (hssid == 0)
        {
            /* according sgmac is : Xaui8,Xaui9*/
            sgmac_mode.cfg_sgmac8_sel_hss2 = 0;
            sgmac_mode.cfg_sgmac9_sel_hss2 = 0;
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 8);
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 9);
        }
        else if  (hssid == 1)
        {
            /* according sgmac is : Xaui10,Xaui11*/
            sgmac_mode.cfg_sgmac10_sel_hss0 = 0;
            sgmac_mode.cfg_sgmac10_sel_hss2 = 0;
            sgmac_mode.cfg_sgmac11_sel_hss0 = 0;
            sgmac_mode.cfg_sgmac11_sel_hss2 = 0;
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 10);
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 11);
        }
        else if (hssid == 2)
        {
            /* according sgmac is : Xaui6,Xaui7*/
            sgmac_mode.cfg_sgmac6_sel_hss2 = 1;
            sgmac_mode.cfg_sgmac7_sel_hss2 = 1;
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 6);
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 7);
        }
        else
        {
            /* according sgmac is : Xaui4,Xaui5*/
            sgmac_mode.cfg_sgmac4_xaui_en = 1;
            sgmac_mode.cfg_sgmac5_xaui_en = 1;
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 4);
            sgmac_mode.cfg_sgmac_xfi_dis |= (1 << 5);
        }
    }
    else if (mode == DRV_SERDES_XGSGMII_MODE)
    {
         /* Config SgmacModeCfg */
        if (hssid == 0)
        {
            /* according sgmac is : Xaui8,Xaui9 ---> sgmac56,57,58,59 */
            sgmac_mode.cfg_sgmac10_sel_hss0 = 1;
            sgmac_mode.cfg_sgmac10_sel_hss2 = 0;
            sgmac_mode.cfg_sgmac11_sel_hss0 = 1;
            sgmac_mode.cfg_sgmac11_sel_hss2 = 0;
        }
        else if  (hssid == 1)
        {
            /* according sgmac is : Xaui10,Xaui11 --->sgmac58,59*/
            /* no need to change, sgmac10 and sgmac11 is also used hss1 */
        }
        else if (hssid == 2)
        {
            /* according sgmac is : Xaui6,Xaui7--->sgmac54,55,56,57,58,59*/
            sgmac_mode.cfg_sgmac10_sel_hss0 = 0;
            sgmac_mode.cfg_sgmac10_sel_hss2 = 1;
            sgmac_mode.cfg_sgmac11_sel_hss0 = 0;
            sgmac_mode.cfg_sgmac11_sel_hss2 = 1;
            sgmac_mode.cfg_sgmac8_sel_hss2 = 1;
            sgmac_mode.cfg_sgmac9_sel_hss2 = 1;
        }
        else
        {
            /* according sgmac is : Xaui4,Xaui5---->sgmac48,49,50,51,52,53,54,55*/
            sgmac_mode.cfg_sgmac4_xaui_en = 0;
            sgmac_mode.cfg_sgmac5_xaui_en = 0;
            sgmac_mode.cfg_sgmac6_sel_hss2 = 0;
            sgmac_mode.cfg_sgmac7_sel_hss2 = 0;
        }
    }

    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_set_hss12g_macro_mode(uint8 lchip, uint8 hssid, uint8 mode)
{
    uint32 value_tmp = 0;
    uint32 hsspll = 0;
    uint16 tx_cfg = 0;
    uint16 rx_cfg = 0;
    uint32 int_clk_div = 0;
    uint32 core_clk_div = 0;
    uint8 index = 0;
    uint32 check_value = 0;
    uint8 is_sgmac = 0;
    uint32 refclk = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (hssid > 3)
    {
        return CTC_E_INVALID_PARAM;
    }

    refclk = drv_greatbelt_get_clock(lchip, DRV_TANK_REF_CLOCK_TYPE);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "TankPll Refclk:%d \n", refclk);

    if ((DRV_SERDES_XAUI_MODE == mode) || (DRV_SERDES_2DOT5_MODE == mode))
    {
        if (refclk == 156)
        {
            hsspll  = 0xde22;
        }
        else if (refclk == 125)
        {
            hsspll  = 0xdb22;
        }
        else
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll Refclk:%d not support!\n", refclk);
            return CTC_E_NOT_SUPPORT;
        }

        tx_cfg = 0x6;
        rx_cfg = 0x46;
        int_clk_div  = 0x00000302;
        core_clk_div = 0x00000102;
        is_sgmac = 1;
    }
    else if ((DRV_SERDES_XGSGMII_MODE == mode) || (DRV_SERDES_SGMII_MODE == mode))
    {
        if (DRV_SERDES_XGSGMII_MODE == mode)
        {
            is_sgmac = 1;
        }

        if (refclk == 156)
        {
            hsspll  = 0xd802;
        }
        else if (refclk == 125)
        {
            hsspll  = 0xde02;
        }
        else
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll Refclk:%d not support!\n", refclk);
            return CTC_E_NOT_SUPPORT;
        }
        tx_cfg = 0x07;
        rx_cfg = 0x47;
        int_clk_div = 0x00000702;
        core_clk_div = 0x00000302;
    }
    else
    {
        /* others not support , if need please coding here */
        return CTC_E_NOT_SUPPORT;
    }

    /* reset hss and clock tree */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, 0x00000008, &value_tmp));
    value_tmp |= (1 << hssid);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x00000008, value_tmp));

    /* reset ClkDivCore*cfg coreclkreset */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, (0x00000244+hssid*8), &value_tmp));
    value_tmp |= 1;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x00000244+hssid*8), value_tmp));

    /* reset ClkDivIntf*cfg IntfClkReset */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, (0x00000240+hssid*8), &value_tmp));
    value_tmp |= 1;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x00000240+hssid*8), value_tmp));

    /* cfg hss pll */
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x00000100+0x10*hssid), hsspll));

    /* release hss reset */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, 0x00000008, &value_tmp));
    value_tmp &= ~(1 << hssid);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x00000008, value_tmp));

    sal_task_sleep(100);

    DATAPATH_READ_CHIP(lchip, 0x0000000c, &value_tmp);

    check_value = ((1<<(hssid+4))|0x01);
    if ((value_tmp & check_value) != check_value)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "PLLLockOut status:0x%x , check_value:0x%x\n", value_tmp, check_value);
        return CTC_E_UNEXPECT;
    }

    /* cfg hss internal register, broadcast to all lane */
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hssid, (0x11 << 6)|0, tx_cfg));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hssid, (0x12 << 6)|0, rx_cfg));

    /* cfg clock diver */
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x00000240+hssid*8), int_clk_div));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x00000244+hssid*8), core_clk_div));

    /* Modify the HssModeCfg */
    value_tmp = 0;
    for (index = 0; index < 8; index++)
    {
        value_tmp |=  (mode << (index*4));
    }
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x000001d0+hssid*4), value_tmp));

    /* Modification the AuxClkSelCfg to not use the AuxCLk */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, 0x268, &value_tmp));
    value_tmp |= (0xff << hssid*8);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, value_tmp));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, 0x26c, &value_tmp));
    value_tmp |= (0xff << hssid*8);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, value_tmp));

    if (is_sgmac)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_chip_set_sgmac_mode_cfg(lchip, hssid, mode));
    }

    sal_task_sleep(100);

    /* reset HssTxGearBoxRstCtl GearBoxReset */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, 0x140, &value_tmp));
    value_tmp |= (1 << hssid);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x140, value_tmp));

    sal_task_sleep(10);

    /* Release the GearBox Reset  */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, 0x140, &value_tmp));
    value_tmp &= ~(1 << hssid);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x140, value_tmp));

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_get_cpu_eth_en(uint8 lchip, uint8 *enable)
{
    *enable = p_gb_chip_master[lchip]->cpu_eth_en;

    return CTC_E_NONE;
}

#define __SHOW_INFO__

/**
 @brief The function is to show ecc recover status
*/
int32
sys_greatbelt_chip_show_ecc_recover_status(uint8 lchip, uint8 is_all)
{
    CTC_ERROR_RETURN(drv_greatbelt_ecc_recover_show_status(lchip, is_all));

    return CTC_E_NONE;
}

/**
 @brief The function is to show chip status
*/
int32
sys_greatbelt_chip_show_status(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    uint8 gchip = 0;
    uint8 chip_num = 0;
    ctc_chip_access_type_t access_type;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    chip_num = sys_greatbelt_get_local_chip_num(lchip);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n The lchip number is: %d\n", chip_num);


        CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " The global chip id for lchip %d is: %d\n", lchip, gchip);

    CTC_ERROR_RETURN(sys_greatbelt_chip_get_access_type(lchip, &access_type));
    if (access_type == CTC_CHIP_PCI_ACCESS)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " The current access mode is : PCIe! \n");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " The current access mode is :I2C! \n");
    }

        sys_greatbelt_get_gchip_id(lchip, &gchip);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n GCHIP 0x%04X chip recover status:\n", gchip);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, " ------------------------------------\n");
        CTC_ERROR_RETURN(sys_greatbelt_chip_show_ecc_recover_status(lchip, 0));
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    return ret;
}

#define __MDIO_INTERFACE__
/**
 @brief set phy to port mapping
*/
int32
sys_greatbelt_chip_set_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para)
{
    uint16 lport = 0;

    if (NULL == phy_mapping_para)
    {
        return CTC_E_INVALID_PTR;
    }

    sal_memcpy(p_gb_chip_master[lchip]->port_mdio_mapping_tbl, phy_mapping_para->port_mdio_mapping_tbl,
               SYS_GB_MAX_PHY_PORT);

    sal_memcpy(p_gb_chip_master[lchip]->port_phy_mapping_tbl, phy_mapping_para->port_phy_mapping_tbl,
               SYS_GB_MAX_PHY_PORT);

    for(lport=0; lport<SYS_GB_MAX_PHY_PORT; lport++)
    {
        if((0xff==p_gb_chip_master[lchip]->port_mdio_mapping_tbl[lport]) || (0xff==p_gb_chip_master[lchip]->port_phy_mapping_tbl[lport]))
        {
            continue;
        }
        else
        {
            if((p_gb_chip_master[lchip]->port_mdio_mapping_tbl[lport])>=4 || (p_gb_chip_master[lchip]->port_phy_mapping_tbl[lport])>=32)
            {
                return CTC_E_INVALID_EXP_VALUE;
            }
            p_gb_chip_master[lchip]->mdio_phy_port_mapping_tbl[(p_gb_chip_master[lchip]->port_mdio_mapping_tbl[lport])][(p_gb_chip_master[lchip]->port_phy_mapping_tbl[lport])] = lport;
        }
    }

    return CTC_E_NONE;
}

/**
 @brief get phy to port mapping
*/
int32
sys_greatbelt_chip_get_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* p_phy_mapping_para)
{
    if (NULL == p_phy_mapping_para)
    {
        return CTC_E_INVALID_PTR;
    }

    sal_memcpy(p_phy_mapping_para->port_mdio_mapping_tbl,  p_gb_chip_master[lchip]->port_mdio_mapping_tbl,
               SYS_GB_MAX_PHY_PORT);

    sal_memcpy(p_phy_mapping_para->port_phy_mapping_tbl,  p_gb_chip_master[lchip]->port_phy_mapping_tbl,
               SYS_GB_MAX_PHY_PORT);

    return CTC_E_NONE;
}

/**
 @brief get mdio and phy info base on port
*/
int32
sys_greatbelt_chip_get_phy_mdio(uint8 lchip, uint16 gport, uint8* mdio_bus, uint8* phy_addr)
{

    if ((gport > SYS_GB_MAX_PHY_PORT - 1))
    {
        return CTC_E_INVALID_PARAM;
    }

    *mdio_bus = p_gb_chip_master[lchip]->port_mdio_mapping_tbl[gport];
    *phy_addr = p_gb_chip_master[lchip]->port_phy_mapping_tbl[gport];

    return CTC_E_NONE;
}

/**
 @brief get port info base on mdio and phy
*/
int32
sys_greatbelt_chip_get_lport(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint16* lport)
{
    if(mdio_bus >= 4 || phy_addr >= 32)
    {
        return CTC_E_INVALID_EXP_VALUE;
    }
    *lport = p_gb_chip_master[lchip]->mdio_phy_port_mapping_tbl[mdio_bus][phy_addr];

    return CTC_E_NONE;
}

/**
 @brief write gephy interface
*/
int32
sys_greatbelt_chip_write_gephy_reg(uint8 lchip, uint16 gport, ctc_chip_gephy_para_t* p_gephy_para)
{
    uint8 mdio_bus = 0;
    uint8 phy_addr = 0;
    uint8 lport = 0;
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_gephy_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write Gephy, gport:%d  reg:%d val:%d\n",  \
                     gport, p_gephy_para->reg, p_gephy_para->val);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    ret = sys_greatbelt_chip_get_phy_mdio(lchip, lport, &mdio_bus, &phy_addr);
    if (CTC_E_NONE != ret)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((SYS_CHIP_MDIO_BUS0 != mdio_bus) && (SYS_CHIP_MDIO_BUS1 != mdio_bus))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_peri_write_gephy_reg(lchip, mdio_bus, phy_addr, p_gephy_para->reg, p_gephy_para->val));

    return CTC_E_NONE;
}

/**
 @brief read gephy interface
*/
int32
sys_greatbelt_chip_read_gephy_reg(uint8 lchip, uint16 gport, ctc_chip_gephy_para_t* p_gephy_para)
{
    uint8 mdio_bus = 0;
    uint8 phy_addr = 0;
    uint8 lport = 0;
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_gephy_para);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Read Gephy, gport:%d  reg:%d \n", gport, p_gephy_para->reg);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    ret = sys_greatbelt_chip_get_phy_mdio(lchip, lport, &mdio_bus, &phy_addr);
    if (CTC_E_NONE != ret)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((SYS_CHIP_MDIO_BUS0 != mdio_bus) && (SYS_CHIP_MDIO_BUS1 != mdio_bus))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_peri_read_gephy_reg(lchip, mdio_bus, phy_addr, p_gephy_para->reg, &(p_gephy_para->val)));

    return CTC_E_NONE;
}

/**
 @brief write xgphy interface
*/
int32
sys_greatbelt_chip_write_xgphy_reg(uint8 lchip, uint16 gport, ctc_chip_xgphy_para_t* p_xgphy_para)
{
    uint8 mdio_bus = 0;
    uint8 phy_addr = 0;
    uint8 lport = 0;
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_xgphy_para);

    if (0 == p_xgphy_para->devno)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Write Xgphy, gport:%d  dev:%d reg:%d val:%d \n",
                     gport, p_xgphy_para->devno, p_xgphy_para->reg, p_xgphy_para->val);
    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    ret = sys_greatbelt_chip_get_phy_mdio(lchip, lport, &mdio_bus, &phy_addr);
    if (CTC_E_NONE != ret)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((SYS_CHIP_MDIO_BUS2 != mdio_bus) && (SYS_CHIP_MDIO_BUS3 != mdio_bus))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_peri_write_xgphy_reg(lchip, mdio_bus, phy_addr, p_xgphy_para->devno,
                                              p_xgphy_para->reg, p_xgphy_para->val));

    return CTC_E_NONE;
}

/**
 @brief read xgphy interface
*/
int32
sys_greatbelt_chip_read_xgphy_reg(uint8 lchip, uint16 gport, ctc_chip_xgphy_para_t* p_xgphy_para)
{
    uint8 mdio_bus = 0;
    uint8 phy_addr = 0;
    uint8 lport = 0;
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_xgphy_para);

    if (0 == p_xgphy_para->devno)
    {
        return CTC_E_INVALID_PARAM;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Read Xgphy, gport:%d  dev:%d reg:%d \n",
                     gport, p_xgphy_para->devno, p_xgphy_para->reg);

    SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport);

    ret = sys_greatbelt_chip_get_phy_mdio(lchip, lport, &mdio_bus, &phy_addr);
    if (CTC_E_NONE != ret)
    {
        return CTC_E_INVALID_PARAM;
    }

    if ((SYS_CHIP_MDIO_BUS2 != mdio_bus) && (SYS_CHIP_MDIO_BUS3 != mdio_bus))
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_peri_read_xgphy_reg(lchip, mdio_bus, phy_addr, p_xgphy_para->devno,
                                             p_xgphy_para->reg, &(p_xgphy_para->val)));
    return CTC_E_NONE;
}

/**
 @brief set gephy auto scan optional reg
*/
int32
sys_greatbelt_chip_set_gephy_scan_special_reg(uint8 lchip, ctc_chip_ge_opt_reg_t* p_gephy_opt, bool enable)
{
    drv_peri_ge_opt_reg_t drv_para;

    CTC_PTR_VALID_CHECK(p_gephy_opt);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Auto Scan Reg:%d  bit:%d intr:%d enable:%d\n",
                     p_gephy_opt->reg, p_gephy_opt->bit_ctl, p_gephy_opt->intr_enable, ((TRUE == enable) ? 1 : 0));




    drv_para.bit_ctl = p_gephy_opt->bit_ctl;
    drv_para.intr_enable = p_gephy_opt->intr_enable;
    drv_para.reg = p_gephy_opt->reg;

    if (enable)
    {
        if (p_gb_chip_master[lchip]->first_ge_opt_reg_used == 0)
        {
            drv_para.reg_index = 0;
            p_gb_chip_master[lchip]->first_ge_opt_reg_used = 1;
            p_gb_chip_master[lchip]->first_ge_opt_reg = p_gephy_opt->reg;
        }
        else if (p_gb_chip_master[lchip]->second_ge_opt_reg_used == 0)
        {
            drv_para.reg_index = 1;
            p_gb_chip_master[lchip]->second_ge_opt_reg_used = 1;
            p_gb_chip_master[lchip]->second_ge_opt_reg = p_gephy_opt->reg;
        }
        else
        {
            return CTC_E_CHIP_SCAN_PHY_REG_FULL;
        }
    }
    else
    {
        if ((p_gb_chip_master[lchip]->first_ge_opt_reg == p_gephy_opt->reg) && (p_gb_chip_master[lchip]->first_ge_opt_reg_used))
        {
            drv_para.reg_index = 0;
            p_gb_chip_master[lchip]->first_ge_opt_reg_used = 0;
            p_gb_chip_master[lchip]->first_ge_opt_reg = 0;
        }
        else if ((p_gb_chip_master[lchip]->second_ge_opt_reg == p_gephy_opt->reg) && (p_gb_chip_master[lchip]->second_ge_opt_reg_used))
        {
            drv_para.reg_index = 1;
            p_gb_chip_master[lchip]->second_ge_opt_reg_used = 0;
            p_gb_chip_master[lchip]->second_ge_opt_reg = 0;
        }
        else
        {
            return CTC_E_MEMBER_NOT_EXIST;
        }
    }

     CTC_ERROR_RETURN(drv_greatbelt_peri_set_gephy_scan_special_reg(lchip, &drv_para, enable));

    return CTC_E_NONE;
}

/**
 @brief set xgphy auto scan optional reg
*/
int32
sys_greatbelt_chip_set_xgphy_scan_special_reg(uint8 lchip, ctc_chip_xg_opt_reg_t* p_xgphy_opt, uint8 enable)
{
    drv_peri_xg_opt_reg_t drv_para;

    CTC_PTR_VALID_CHECK(p_xgphy_opt);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Auto Scan Dev:%d Reg:%d  bit:%d intr:%d enable:%d\n",
                     p_xgphy_opt->dev, p_xgphy_opt->reg, p_xgphy_opt->bit_ctl, p_xgphy_opt->intr_enable, ((TRUE == enable) ? 1 : 0));


    drv_para.bit_ctl = p_xgphy_opt->bit_ctl;
    drv_para.intr_enable = p_xgphy_opt->intr_enable;
    drv_para.reg = p_xgphy_opt->reg;
    drv_para.dev = p_xgphy_opt->dev;

    CTC_ERROR_RETURN(drv_greatbelt_peri_set_xgphy_scan_special_reg(lchip, &drv_para, enable));

    return CTC_E_NONE;
}

/**
 @brief smi interface for set auto scan para
*/
int32
sys_greatbelt_chip_set_phy_scan_para(uint8 lchip, ctc_chip_phy_scan_ctrl_t* p_scan_para)
{
    drv_peri_phy_scan_ctrl_t drv_para;

    CTC_PTR_VALID_CHECK(p_scan_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Scan Para op_flag:0x%x ge_bitmap0:0x%x  ge_bitmap1:0x%x interval:0x%x   \
        xgbitmap0:0x%x xgbitmap1:0x%x bitmask:0x%x scan_twince:%d\n",
                     p_scan_para->op_flag, p_scan_para->scan_gephy_bitmap0, p_scan_para->scan_gephy_bitmap1, p_scan_para->scan_interval,
                     p_scan_para->scan_xgphy_bitmap0, p_scan_para->scan_xgphy_bitmap1, p_scan_para->xgphy_link_bitmask, p_scan_para->xgphy_scan_twice);


    drv_para.op_flag = p_scan_para->op_flag;
    drv_para.scan_gephy_bitmap0 = p_scan_para->scan_gephy_bitmap0;
    drv_para.scan_gephy_bitmap1 = p_scan_para->scan_gephy_bitmap1;
    drv_para.scan_interval = p_scan_para->scan_interval;
    drv_para.scan_xgphy_bitmap0 = p_scan_para->scan_xgphy_bitmap0;
    drv_para.scan_xgphy_bitmap1 = p_scan_para->scan_xgphy_bitmap1;
    drv_para.xgphy_link_bitmask = p_scan_para->xgphy_link_bitmask;
    drv_para.xgphy_scan_twice = p_scan_para->xgphy_scan_twice;
    drv_para.mdio_use_phy0 = p_scan_para->mdio_use_phy0;
    drv_para.mdio_use_phy1 = p_scan_para->mdio_use_phy1;

    CTC_ERROR_RETURN(drv_greatbelt_peri_set_phy_scan_para(lchip, &drv_para));

    return CTC_E_NONE;
}

/**
 @brief smi interface for set auto scan enable or not
*/
int32
sys_greatbelt_chip_set_phy_scan_en(uint8 lchip, bool enable)
{
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Scan Enable enable:%d\n", ((TRUE == enable) ? 1 : 0));
    CTC_ERROR_RETURN(drv_greatbelt_peri_set_phy_scan_en(lchip, enable));

    return CTC_E_NONE;
}

/**
 @brief mac led interface
*/
int32
sys_greatbelt_chip_mdio_read(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para)
{
    CTC_PTR_VALID_CHECK(p_para);

    switch(type)
    {
        case CTC_CHIP_MDIO_GE:
            CTC_ERROR_RETURN(drv_greatbelt_peri_read_gephy_reg(lchip, p_para->bus, p_para->phy_addr,
                                    p_para->reg, &(p_para->value)));
            break;

        case CTC_CHIP_MDIO_XG:
            CTC_ERROR_RETURN(drv_greatbelt_peri_read_xgphy_reg(lchip, p_para->bus, p_para->phy_addr,
                                    p_para->dev_no, p_para->reg, &(p_para->value)));
            break;

        case CTC_CHIP_MDIO_XGREG_BY_GE:
            CTC_ERROR_RETURN(drv_greatbelt_peri_read_gephy_xgreg(lchip, p_para->bus, p_para->phy_addr,
                                   p_para->dev_no, p_para->reg, &(p_para->value)));
            break;
        default:
            break;
    }

    return CTC_E_NONE;
}

/**
 @brief mdio interface
*/
int32
sys_greatbelt_chip_mdio_write(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para)
{
    CTC_PTR_VALID_CHECK(p_para);

    switch(type)
    {
        case CTC_CHIP_MDIO_GE:
            CTC_ERROR_RETURN(drv_greatbelt_peri_write_gephy_reg(lchip, p_para->bus, p_para->phy_addr,
                                    p_para->reg, p_para->value));
            break;

        case CTC_CHIP_MDIO_XG:
            CTC_ERROR_RETURN(drv_greatbelt_peri_write_xgphy_reg(lchip, p_para->bus, p_para->phy_addr,
                                    p_para->dev_no, p_para->reg, p_para->value));
            break;

        case CTC_CHIP_MDIO_XGREG_BY_GE:
            CTC_ERROR_RETURN(drv_greatbelt_peri_write_gephy_xgreg(lchip, p_para->bus, p_para->phy_addr,
                                    p_para->dev_no, p_para->reg, p_para->value));
            break;
        default:
            break;
    }

    return CTC_E_NONE;
}

/**
 @brief mdio interface to set mdio clock frequency
*/
int32
sys_greatbelt_chip_set_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16 freq)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 core_freq = 0;

    if (type > CTC_CHIP_MDIO_XGREG_BY_GE)
    {
        return CTC_E_INVALID_PARAM;
    }


    if ((type == CTC_CHIP_MDIO_GE) || (type == CTC_CHIP_MDIO_XGREG_BY_GE))
    {
        /* for 1g mdio bus */
            core_freq = sys_greatbelt_get_core_freq(lchip);
            field_val = (core_freq*1000)/freq;
            cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_Mdio1GClkDiv_f);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            field_val = 1;
            cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_ResetMdio1GClkDiv_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
            field_val = 0;
            cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_ResetMdio1GClkDiv_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        /* for xg mdio bus */
            core_freq = sys_greatbelt_get_core_freq(lchip);
            field_val = (core_freq*1000)/freq;
            cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_MdioXGClkDiv_f);
            DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));

            field_val = 1;
            cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_ResetMdioXGClkDiv_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
            field_val = 0;
            cmd = DRV_IOW(MdioClockCfg_t, MdioClockCfg_ResetMdioXGClkDiv_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    return CTC_E_NONE;
}

/**
 @brief mdio interface to set mdio clock frequency
*/
int32
sys_greatbelt_chip_get_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16* freq)
{
    uint32 cmd = 0;
    uint32 field_val = 0;
    uint32 core_freq = 0;

    if (type > CTC_CHIP_MDIO_XGREG_BY_GE)
    {
        return CTC_E_INVALID_PARAM;
    }

    core_freq = sys_greatbelt_get_core_freq(0);

    if ((type == CTC_CHIP_MDIO_GE) || (type == CTC_CHIP_MDIO_XGREG_BY_GE))
    {
        /* for 1g mdio bus */
        cmd = DRV_IOR(MdioClockCfg_t, MdioClockCfg_Mdio1GClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }
    else
    {
        /* for xg mdio bus */
        cmd = DRV_IOR(MdioClockCfg_t, MdioClockCfg_MdioXGClkDiv_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_val));
    }

    *freq = core_freq*1000/field_val;

    return CTC_E_NONE;
}


/*intlk_mode: 0-packet mode, 1-segment mode, 2-fabric mode */
int32
sys_greatbelt_chip_intlk_register_init(uint8 lchip, uint8 intlk_mode)
{
    uint32 cmd = 0;
    uint32 val = 0;
    uint8 internal_mode = 0;
    uint16 init_cnt = 0;
    uint8 init_done = 0;
    drv_work_platform_type_t platform_type;
    net_tx_int_lk_ctl_t nettx_intlk_ctl;
    net_rx_intlk_chan_en_t netrx_intlk_en;
    int_lk_intf_cfg_t intlk_intf_cfg;
    int_lk_misc_ctl_t intlk_misc_ctl;
    int_lk_lane_rx_ctl_t intlk_lane_rx;
    int_lk_lane_tx_ctl_t intlk_lane_tx;
    int_lk_mem_init_ctl_t intlk_mem_init;
    int_lk_lane_swap_en_t intlk_lane_swap;
    int_lk_rate_match_ctl_t intlk_rate_ctl;
    int_lk_soft_reset_t intlk_reset;
    ipe_mux_header_adjust_ctl_t ipe_header_ctl;

    internal_mode = (intlk_mode==1)?0:1;

    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));

    sal_memset(&nettx_intlk_ctl, 0, sizeof(net_tx_int_lk_ctl_t));
    sal_memset(&netrx_intlk_en, 0, sizeof(net_rx_intlk_chan_en_t));
    sal_memset(&intlk_intf_cfg, 0, sizeof(int_lk_intf_cfg_t));
    sal_memset(&intlk_misc_ctl, 0, sizeof(int_lk_misc_ctl_t));
    sal_memset(&intlk_lane_rx, 0, sizeof(int_lk_lane_rx_ctl_t));
    sal_memset(&intlk_lane_tx, 0, sizeof(int_lk_lane_tx_ctl_t));
    sal_memset(&intlk_mem_init, 0, sizeof(int_lk_mem_init_ctl_t));
    sal_memset(&intlk_lane_swap, 0, sizeof(int_lk_lane_swap_en_t));
    sal_memset(&intlk_rate_ctl, 0, sizeof(int_lk_rate_match_ctl_t));
    sal_memset(&intlk_reset, 0, sizeof(int_lk_soft_reset_t));

    cmd = DRV_IOR(NetTxIntLkCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &nettx_intlk_ctl));
    nettx_intlk_ctl.int_lk_pkt_mode_tx_en = internal_mode;
    nettx_intlk_ctl.int_lk_fabric_mode = (intlk_mode==2)?1:0;
    nettx_intlk_ctl.int_lk_fabric_local_phy_port = (intlk_mode==2)?SYS_RESERVE_PORT_ID_INTLK:0;
    cmd = DRV_IOW(NetTxIntLkCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &nettx_intlk_ctl));

    cmd = DRV_IOR(IntLkIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_intf_cfg));
    intlk_intf_cfg.int_lk_intf_en = 1;
    intlk_intf_cfg.int_lk_pkt_mode = internal_mode;
    intlk_intf_cfg.int_lk_slave_mode = (intlk_mode==2)?0:1;
    cmd = DRV_IOW(IntLkIntfCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_intf_cfg));

    netrx_intlk_en.int_lk_en = 1;
    cmd = DRV_IOW(NetRxIntlkChanEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &netrx_intlk_en));

    cmd = DRV_IOR(IntLkMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_misc_ctl));
    intlk_misc_ctl.cfg_packet_mode = internal_mode;
    cmd = DRV_IOW(IntLkMiscCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_misc_ctl));

    cmd = DRV_IOR(IntLkLaneRxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_lane_rx));
    intlk_lane_rx.cfg_lane_rx_en = 0xff;
    intlk_lane_rx.lane_rx_bit_order_invert = 1;
    intlk_lane_rx.lane_rx_sig_det_active_value = 1;
    cmd = DRV_IOW(IntLkLaneRxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_lane_rx));

    cmd = DRV_IOR(IntLkLaneTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_lane_tx));
    intlk_lane_tx.cfg_lane_tx_en = 0xff;
    intlk_lane_tx.cfg_tx_end_lane_num = 7;
    cmd = DRV_IOW(IntLkLaneTxCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_lane_tx));

    val = 1;
    cmd = DRV_IOW(IntLkMemInitCtl_t, IntLkMemInitCtl_Init_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &val));

    if (HW_PLATFORM == platform_type)
    {
         /* wait for init */
        while (init_cnt < 4000)
        {
            cmd = DRV_IOR(IntLkMemInitCtl_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_mem_init));

            /* init failed */
            if (intlk_mem_init.init_done)
            {
                init_done = TRUE;
                break;
            }

            init_cnt++;
        }

        if (init_done == FALSE)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "intlk init falied, init not done!\n");
            return CTC_E_NOT_INIT;
        }
    }

    intlk_lane_swap.lane_swap_en = 1;
    cmd = DRV_IOW(IntLkLaneSwapEn_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_lane_swap));

    cmd = DRV_IOR(IntLkRateMatchCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_rate_ctl));
    intlk_rate_ctl.max_rate_cycle_cnt = 100;
    cmd = DRV_IOW(IntLkRateMatchCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_rate_ctl));

    /*just do release tx reset, rx and align release do by caller*/
    cmd = DRV_IOR(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));
    intlk_reset.soft_reset_ltx = 0;
    cmd = DRV_IOW(IntLkSoftReset_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &intlk_reset));

    sal_memset(&ipe_header_ctl, 0, sizeof(ipe_mux_header_adjust_ctl_t));
    cmd = DRV_IOR(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_ctl));

    if (intlk_mode == 2)
    {
        ipe_header_ctl.inter_laken_local_phy_port_base = 0;
        ipe_header_ctl.inter_laken_packet_header_en      = 1;
    }
    else
    {
        /* refer to SYS_INTLK_PORT_BASE_SEGMENT/SYS_INTLK_PORT_BASE_PKT */
        ipe_header_ctl.inter_laken_local_phy_port_base =
            (intlk_mode?(24):(76));
    }

    cmd = DRV_IOW(IpeMuxHeaderAdjustCtl_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &ipe_header_ctl));

    return CTC_E_NONE;
}

#define __I2C_MASTER_INTERFACE__
/**
 @brief i2c master for read sfp
*/
int32
sys_greatbelt_chip_i2c_read(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    drv_peri_i2c_read_t drv_para;
    uint8 index = 0;
    uint32 total_len = 0;
    int32 ret = 0;

    CTC_PTR_VALID_CHECK(p_i2c_para);
    CTC_PTR_VALID_CHECK(p_i2c_para->p_buf);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "read sfp, bitmap:0x%x addr:0x%x  offset:0x%x length:0x%x \n",
                     p_i2c_para->slave_bitmap, p_i2c_para->dev_addr, p_i2c_para->offset, p_i2c_para->length);


    /* check, bitmap should not be 0 */
    if(!p_i2c_para->slave_bitmap)
    {
        return CTC_E_INVALID_PARAM;
    }

    for (index = 0; index < 32; index++)
    {
        if (((p_i2c_para->slave_bitmap) >> index) & 0x1)
        {
            total_len += p_i2c_para->length;
        }
    }

    drv_para.buf = (uint8*)mem_malloc(MEM_SYSTEM_MODULE, total_len);
    if (NULL == drv_para.buf)
    {
        return CTC_E_NO_MEMORY;
    }

    drv_para.dev_addr = p_i2c_para->dev_addr;
    drv_para.length = p_i2c_para->length;
    drv_para.offset = p_i2c_para->offset;
    drv_para.slave_bitmap = p_i2c_para->slave_bitmap;

    CTC_ERROR_GOTO(drv_greatbelt_peri_i2c_read(lchip, &drv_para), ret, error);

    sal_memcpy(p_i2c_para->p_buf, drv_para.buf, total_len);
error:
    mem_free(drv_para.buf);
    drv_para.buf = NULL;

    return ret;
}

/**
 @brief i2c master for write sfp
*/
int32
sys_greatbelt_chip_i2c_write(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para)
{
    drv_peri_i2c_write_t drv_para;

    CTC_PTR_VALID_CHECK(p_i2c_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "write sfp, slave_id:0x%x addr:0x%x  offset:0x%x data:0x%x \n",
                     p_i2c_para->slave_id, p_i2c_para->dev_addr, p_i2c_para->offset, p_i2c_para->data);

    drv_para.data = p_i2c_para->data;
    drv_para.dev_addr = p_i2c_para->dev_addr;
    drv_para.offset = p_i2c_para->offset;
    drv_para.slave_id = p_i2c_para->slave_id;

    CTC_ERROR_RETURN(drv_greatbelt_peri_i2c_write(lchip, &drv_para));

    return CTC_E_NONE;
}

/**
 @brief i2c master for polling read
*/
int32
sys_greatbelt_chip_set_i2c_scan_para(uint8 lchip, ctc_chip_i2c_scan_t* p_i2c_para)
{
    drv_peri_i2c_scan_t drv_scan_para;

    CTC_PTR_VALID_CHECK(p_i2c_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "scan sfp, op_flag:0x%x, bitmap:0x%x addr:0x%x  offset:0x%x length:0x%x intreval:0x%x\n",
                     p_i2c_para->op_flag, p_i2c_para->slave_bitmap[0], p_i2c_para->dev_addr, p_i2c_para->offset, p_i2c_para->length, p_i2c_para->interval);

    drv_scan_para.dev_addr = p_i2c_para->dev_addr;
    drv_scan_para.interval = p_i2c_para->interval;
    drv_scan_para.length = p_i2c_para->length;
    drv_scan_para.offset = p_i2c_para->offset;
    drv_scan_para.op_flag = p_i2c_para->op_flag;
    drv_scan_para.slave_bitmap = p_i2c_para->slave_bitmap[0];

    CTC_ERROR_RETURN(drv_greatbelt_peri_set_i2c_scan_para(lchip, &drv_scan_para));

    return CTC_E_NONE;
}

/**
 @brief i2c master for polling read start
*/
int32
sys_greatbelt_chip_set_i2c_scan_en(uint8 lchip, bool enable)
{

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Set Scan Enable enable:%d\n", ((TRUE == enable) ? 1 : 0));
    CTC_ERROR_RETURN(drv_greatbelt_peri_set_i2c_scan_en(lchip, enable));

    return CTC_E_NONE;
}

/**
 @brief interface for read i2c databuf, usual used for i2c master for polling read
*/
int32
sys_greatbelt_chip_read_i2c_buf(uint8 lchip, ctc_chip_i2c_scan_read_t *p_i2c_scan_read)
{

    CTC_PTR_VALID_CHECK(p_i2c_scan_read);

    if (TRUE != sys_greatbelt_chip_is_local(lchip, p_i2c_scan_read->gchip))
    {
        return CTC_E_INVALID_LOCAL_CHIPID;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "read sfp buf, lchip:%d len:%d\n", lchip, p_i2c_scan_read->len);

    CTC_ERROR_RETURN(drv_greatbelt_peri_read_i2c_buf(lchip, p_i2c_scan_read->len, p_i2c_scan_read->p_buf));

    return CTC_E_NONE;
}

#define __MAC_LED__
/**
 @brief mac led interface
*/
int32
sys_greatbelt_chip_set_mac_led_mode(uint8 lchip, ctc_chip_led_para_t* p_led_para, ctc_chip_mac_led_type_t led_type, uint8 inner)
{
    drv_peri_led_para_t drv_led_para;

    CTC_PTR_VALID_CHECK(p_led_para);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "op_flag:0x%x port_id:0x%x polarity:0x%x first_mode:0x%x scd_mode:0x%x\n",
                     p_led_para->op_flag, p_led_para->port_id, p_led_para->polarity, p_led_para->first_mode, p_led_para->sec_mode);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "led_type:%d \n", led_type);

    sal_memset(&drv_led_para, 0, sizeof(drv_peri_led_para_t));

    /* get lchip id from port id */
    drv_led_para.port_id = p_led_para->port_id;
    drv_led_para.op_flag = p_led_para->op_flag;
    drv_led_para.first_mode = p_led_para->first_mode;
    drv_led_para.polarity = p_led_para->polarity;
    drv_led_para.sec_mode = p_led_para->sec_mode;

    CTC_ERROR_RETURN(drv_greatbelt_peri_set_mac_led_mode(lchip, &drv_led_para, led_type));

    if (0 == inner)
    {
        CTC_ERROR_RETURN(sys_greatbelt_port_store_led_mode_to_lport_attr(lchip, p_led_para->port_id, p_led_para->first_mode, (led_type == CTC_CHIP_USING_TWO_LED)?p_led_para->sec_mode:CTC_CHIP_MAC_LED_MODE));
    }

    return CTC_E_NONE;
}

/**
 @brief mac led interface for mac and led mapping
*/
int32
sys_greatbelt_chip_set_mac_led_mapping(uint8 lchip, ctc_chip_mac_led_mapping_t* p_led_map)
{
    int32 ret = 0;
    uint8 index = 0;

    drv_peri_mac_led_mapping_t drv_led_map;

    CTC_PTR_VALID_CHECK(p_led_map);
    CTC_PTR_VALID_CHECK(p_led_map->p_mac_id);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac num:%d \n", p_led_map->mac_led_num);

    for (index = 0; index < p_led_map->mac_led_num; index++)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac seq %d:%d;  ", index, p_led_map->p_mac_id[index]);
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "\n");

    sal_memset(&drv_led_map, 0, sizeof(drv_peri_mac_led_mapping_t));
    drv_led_map.p_mac_id = (uint8*)mem_malloc(MEM_SYSTEM_MODULE, p_led_map->mac_led_num);
    if (NULL == drv_led_map.p_mac_id)
    {
        return CTC_E_NO_MEMORY;
    }

    sal_memcpy(drv_led_map.p_mac_id, p_led_map->p_mac_id, p_led_map->mac_led_num);
    drv_led_map.mac_led_num = p_led_map->mac_led_num;

    ret = drv_greatbelt_peri_set_mac_led_mapping(lchip, &drv_led_map);

    mem_free(drv_led_map.p_mac_id);
    drv_led_map.p_mac_id = NULL;

    return ret;
}

/**
 @brief begin mac led function
*/
int32
sys_greatbelt_chip_set_mac_led_en(uint8 lchip, bool enable)
{
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "mac led Enable enable:%d\n", ((TRUE == enable) ? 1 : 0));
    CTC_ERROR_RETURN(drv_greatbelt_peri_set_mac_led_en(lchip, enable));

    return CTC_E_NONE;
}

/**
 @brief gpio interface
*/
int32
sys_greatbelt_chip_set_gpio_mode(uint8 lchip, uint8 gpio_id, ctc_chip_gpio_mode_t mode)
{

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "set gpio mode, gpio_id:%d mode:%d\n", gpio_id, mode);

    CTC_ERROR_RETURN(drv_greatbelt_peri_set_gpio_mode(lchip, gpio_id, mode));

    return CTC_E_NONE;
}

/**
 @brief gpio output
*/
int32
sys_greatbelt_chip_set_gpio_output(uint8 lchip, uint8 gpio_id, uint8 out_para)
{

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "set gpio out, gpio_id:%d out_para:%d\n", gpio_id, out_para);

    CTC_ERROR_RETURN(drv_greatbelt_peri_set_gpio_output(lchip, gpio_id, out_para));

    return CTC_E_NONE;
}

/**
 @brief gpio input
*/
int32
sys_greatbelt_chip_get_gpio_intput(uint8 lchip, uint8 gpio_id, uint8* in_value)
{

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(drv_greatbelt_peri_get_gpio_input(lchip, gpio_id, in_value));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "get gpio in, gpio_id:%d in_para:%d\n", gpio_id, *in_value);

    return CTC_E_NONE;
}

/**
 @brief access type switch interface
*/
int32
sys_greatbelt_chip_set_access_type(uint8 lchip, ctc_chip_access_type_t access_type)
{
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "set access, access_type:%d\n", access_type);

    if (access_type >= CTC_CHIP_MAX_ACCESS_TYPE)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(drv_greatbelt_chip_set_access_type(access_type));

    return CTC_E_NONE;
}

/**
 @brief access type switch interface
*/
int32
sys_greatbelt_chip_get_access_type(uint8 lchip, ctc_chip_access_type_t* p_access_type)
{
    drv_access_type_t drv_access;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    CTC_ERROR_RETURN(drv_greatbelt_chip_get_access_type(&drv_access));
    *p_access_type = (ctc_chip_access_type_t)drv_access;

    return CTC_E_NONE;
}

#define __HSS12G_OPERATE__
/**
 @brief hss12g macro reset interface
*/
STATIC int32
_sys_greatbelt_chip_hss12g_macro_release(uint8 lchip, ctc_chip_serdes_mode_t mode, uint8 macro_id)
{
    uint32 cmd = 0;
    hss_pll_reset_cfg_t hss_pll_reset;
    uint32 pll_cfg_low[CTC_CHIP_MAX_SERDES_MODE] = {0x3122, 0x31a2, 0xd802};
    uint32 pll_cfg_high[CTC_CHIP_MAX_SERDES_MODE] = {0x100ff00, 0x100ff00, 0x100ff00};

    if (mode >= CTC_CHIP_MAX_SERDES_MODE)
    {
        return CTC_E_INVALID_PARAM;
    }

    sal_memset(&hss_pll_reset, 0, sizeof(hss_pll_reset_cfg_t));

    /* cfg hss12g hsspll */
    drv_greatbelt_chip_write(lchip, (0x00000100+macro_id*0x10), pll_cfg_low[mode]);
    drv_greatbelt_chip_write(lchip, (0x00000104+macro_id*0x10), pll_cfg_high[mode]);

    /* release hss12g reset */
    cmd = DRV_IOR(HssPllResetCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hss_pll_reset));
    if (macro_id == 0)
    {
        hss_pll_reset.cfg0_h_s_s_r_e_s_e_t = 0;
    }
    else if (macro_id == 1)
    {
        hss_pll_reset.cfg1_h_s_s_r_e_s_e_t = 0;
    }
    else if (macro_id == 2)
    {
        hss_pll_reset.cfg2_h_s_s_r_e_s_e_t = 0;
    }
    else
    {
        hss_pll_reset.cfg3_h_s_s_r_e_s_e_t = 0;
    }
    cmd = DRV_IOW(HssPllResetCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hss_pll_reset));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_hss12g_macro_reset(uint8 lchip, uint8 macro_id)
{
    uint32 cmd = 0;
    hss_pll_reset_cfg_t hss_pll_reset;
    uint32 value_tmp = 0;

    if (macro_id >= HSS_MACRO_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* set hss12g reset */
    sal_memset(&hss_pll_reset, 0, sizeof(hss_pll_reset_cfg_t));
    cmd = DRV_IOR(HssPllResetCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hss_pll_reset));
    if (macro_id == 0)
    {
        hss_pll_reset.cfg0_h_s_s_r_e_s_e_t = 1;
    }
    else if (macro_id == 1)
    {
        hss_pll_reset.cfg1_h_s_s_r_e_s_e_t = 1;
    }
    else if (macro_id == 2)
    {
        hss_pll_reset.cfg2_h_s_s_r_e_s_e_t = 1;
    }
    else
    {
        hss_pll_reset.cfg3_h_s_s_r_e_s_e_t = 1;
    }
    cmd = DRV_IOW(HssPllResetCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &hss_pll_reset));

    /* power down hss12g */
    drv_greatbelt_chip_read(lchip, (0x00000100+macro_id*0x10), &value_tmp);
    value_tmp |= (1<<2);
    drv_greatbelt_chip_write(lchip, (0x00000100+macro_id*0x10), value_tmp);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_hss12g_prbs_test(uint8 lchip, uint8 hss_id, uint8 lane_idx)
{
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint32 field_value = 0;

    tb_id = Hss12G0PrbsTest_t + hss_id*2;

    field_id = lane_idx;

    field_value = 1;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    sal_task_sleep(1);

    field_value = 0;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    return CTC_E_NONE;
}


int32
sys_greatbelt_chip_set_hss12g_enable(uint8 lchip, uint8 hssid, ctc_chip_serdes_mode_t mode, uint8 enable)
{
    int32 ret = CTC_E_NONE;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return ret;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip:%d hssid:0x%x mode:0x%x, enable:0x%x\n", lchip, hssid, mode, enable);

        if (enable)
        {
            CTC_ERROR_RETURN(_sys_greatbelt_chip_hss12g_macro_release(lchip,mode,hssid));
        }
        else
        {
            CTC_ERROR_RETURN(_sys_greatbelt_chip_hss12g_macro_reset(lchip, hssid));
        }

    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Not support on UML \n");
    }

    return CTC_E_NONE;
}

uint32
sys_greatbelt_get_core_freq(uint8 lchip)
{
    uint32 core_freq = 0;

    /*cpu frequence*/
     core_freq = drv_greatbelt_get_clock(lchip, DRV_CORE_CLOCK_TYPE);

    return core_freq;
}

#define __DYNAMIC_SWITCH__
int32
sys_greatbelt_chip_set_serdes_loopback_en(uint8 lchip, uint8 serdes_id, uint8 enable)
{
    uint8 hss_id = 0;
    uint8 lane_idx = 0;
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    drv_datapath_serdes_info_t serdes_info;
    uint8 loop_back_reg = 1;
    uint16 cfg_tmp = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    hss_id = serdes_id/8;
    lane_idx = serdes_info.lane_idx;

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, &cfg_tmp));

    if (enable)
    {
        cfg_tmp |= 0x60;
    }
    else
    {
        cfg_tmp &= 0xff9f;
    }

    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, cfg_tmp));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_get_chann_id_with_mac(uint8 lchip, uint8 mac_id, uint8* chan_id)
{
    uint8 lport = 0;
    uint8 gchip = 0;
    uint16 gport = 0;

    lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id);
    if (0xFF == lport)
    {
        return CTC_E_INVALID_PARAM;
    }

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
    gport = CTC_MAP_LPORT_TO_GPORT(gchip, lport);

    *chan_id = SYS_GET_CHANNEL_ID(lchip, gport);
    if (0xFF == (*chan_id))
    {
        return CTC_E_INVALID_LOCAL_PORT;
    }

    return CTC_E_NONE;

}

STATIC bool
_sys_greatbelt_chip_mix_mode_with_qsgmii(uint8 lchip, uint8 serdes_idx)
{
    uint8 hss_id = serdes_idx/8;
    uint8 index = 0;
    uint8 pcs_mode = 0;
    uint8 sgmii_flag = 0;
    uint8 xsgmii_flag = 0;
    uint8 qsgmii_flag = 0;
    drv_datapath_serdes_info_t serdes_info;

    for (index = hss_id*8; index < hss_id*8 + 8; index++)
    {
        /* neglect itsself */
        if (index == serdes_idx)
        {
            continue;
        }

        sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));
        CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, index, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

        pcs_mode = serdes_info.mode;
        if (DRV_SERDES_NONE_MODE == pcs_mode)
        {
            continue;
        }

        if (!sgmii_flag)
        {
            sgmii_flag = (DRV_SERDES_SGMII_MODE == pcs_mode) ? 1 : 0;
        }

        if (!xsgmii_flag)
        {
            xsgmii_flag = (DRV_SERDES_XGSGMII_MODE == pcs_mode) ? 1 : 0;
        }

        if (!qsgmii_flag)
        {
            qsgmii_flag = (DRV_SERDES_QSGMII_MODE == pcs_mode) ? 1 : 0;
        }

    }

    if (sgmii_flag || xsgmii_flag)
    {
        if (qsgmii_flag)
        {
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }

}


STATIC int32
_sys_greatbelt_chip_set_ignore_failure(uint8 lchip, uint8 mac_id, uint8 value, uint8 mode)
{
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint32 tbl_id = 0;
    uint32 field_value = value ? 1 : 0;

    if (DRV_SERDES_SGMII_MODE == mode)
    {
        tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * (QuadPcsPcs0AnegCfg1_t - QuadPcsPcs0AnegCfg0_t) + (mac_id % 4) * (QuadPcsPcs1AnegCfg0_t - QuadPcsPcs0AnegCfg0_t);
        field_id = QuadPcsPcs0AnegCfg_Pcs0IgnoreLinkFailure_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    }
    else if (DRV_SERDES_XGSGMII_MODE == mode)
    {
        tbl_id = SgmacPcsAnegCfg0_t + mac_id;
        field_id = SgmacPcsAnegCfg_IgnoreLinkFailure_f;
        cmd = DRV_IOW(tbl_id, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
    }
    else
    {
        return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_chip_get_qsgmii_from_serdes(uint8 lchip, uint8 serdes_id, uint8* p_qsgmii_id)
{
    uint8 qsgmii_map[32] =
    {
        0x0,  0x6,  0xff,  0xff,  0x1,  0x7,  0xff,  0xff,
        0x2,  0x8,  0xff,  0xff,  0x3,  0x9,  0xff,  0xff,
        0x4,  0x10,  0x0,  0x06,  0x5,  0x11,  0x1,   0x7,
        0x2,  0x8,   0x3,   0x9,    0xff,0xff, 0xff,  0xff
    };

    if (serdes_id >= 32)
    {
        return CTC_E_EXCEED_MAX_SIZE;
    }

    *p_qsgmii_id = qsgmii_map[serdes_id];

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_to_sgmii(uint8 lchip,uint8 serdes_id, uint8 lane_idx)
{
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint8 enable = 0;
    quad_pcs_pcs0_err_cnt_t errcnt;
    quad_pcs_pcs0_cfg_t quadpcscfg;
    uint16 cfg_tmp = 0;
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    uint8 hss_id = serdes_id/8;
    uint32 temp = 0;
    uint8 failed_cnt = 0;
    uint8 index = 0;
    uint8 qsgmii_id = 0;
    uint8 auxsel_used = 0;
    uint8 gmac_id = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 tbl_step1 = 0;
    uint32 tbl_step2 = 0;
    uint16 ori_tx = 0;
    uint16 ori_rx = 0;
    drv_datapath_serdes_info_t serdes_info;
    hss12_g0_cfg_t g0_cfg;
    hss12_g1_cfg_t g1_cfg;
    hss12_g2_cfg_t g2_cfg;
    hss12_g3_cfg_t g3_cfg;
    sal_memset(&g0_cfg, 0, sizeof(hss12_g0_cfg_t));
    sal_memset(&g1_cfg, 0, sizeof(hss12_g1_cfg_t));
    sal_memset(&g2_cfg, 0, sizeof(hss12_g2_cfg_t));
    sal_memset(&g3_cfg, 0, sizeof(hss12_g3_cfg_t));


    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                            (void*)&serdes_info));

    switch(hss_id)
    {
        case 0:
            cmd = DRV_IOR(Hss12G0Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g0_cfg));
            auxsel_used = g0_cfg.cfg0_h_s_s_e_x_t_c16_s_e_l;
            break;
        case 1:
            cmd = DRV_IOR(Hss12G1Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g1_cfg));
            auxsel_used = g1_cfg.cfg1_h_s_s_e_x_t_c16_s_e_l;
            break;
        case 2:
            cmd = DRV_IOR(Hss12G2Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g2_cfg));
            auxsel_used = g2_cfg.cfg2_h_s_s_e_x_t_c16_s_e_l;
            break;
        case 3:
            cmd = DRV_IOR(Hss12G3Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g3_cfg));
            auxsel_used = g3_cfg.cfg3_h_s_s_e_x_t_c16_s_e_l;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }


    _sys_greatbelt_chip_get_qsgmii_from_serdes(lchip, serdes_id, &qsgmii_id);

   /* Configure the HSS Lane to loopback mode */
    enable = 1;
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_loopback_en(lchip, serdes_id, enable));

    /* Configure the force signal detec of QuadPcs */
    tb_id = QuadPcsPcs0Cfg0_t + (serdes_id/4) + (serdes_id%4)*36;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quadpcscfg));
    quadpcscfg.pcs0_force_signal_detect = 1;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quadpcscfg));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, &ori_tx));
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, &ori_rx));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "original tx:0x%x, rx:0x%x\n", ori_tx, ori_rx);



    /* Configure the HSS Lane to 1G rate(TX/RX) */
    if (auxsel_used)
    {
        /* original pcs mode is QSGMII */
        if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
        {
            cfg_tmp = 0x7;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
            cfg_tmp = 0x47;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
        }
        else
        {
            cfg_tmp = 0x27;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
            cfg_tmp = 0xc7;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
        }
    }
    else
    {
        cfg_tmp = 0x7;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
        cfg_tmp = 0x47;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
    }

    /* Configure the AuxClkSelCfg */
    /* original pcs mode is QSGMII */
    if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
    {
        CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip , 0x268, &temp));
        temp &= ~(1 << serdes_id);
        CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, temp));
    }

    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x26c, &temp));
    temp &= ~(1 << serdes_id);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, temp));

    /* Configure the QsgmiiHssSelCfg(cfgQsgmiiDis/cfgQuad*SgmiiTxSel both = 1) */
    if (0xff != qsgmii_id)
    {
        CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x3c0, &temp));
        temp |= (1 << (qsgmii_id+8));
        temp |= (1 << (qsgmii_id+16));
        CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x3c0, temp));
    }

    /* Configure the HssModeCfg */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, (0x000001d0+hss_id*4), &temp));
    temp &= ~(0xf << ((serdes_id%8)*4));
    temp |= (0x0 << ((serdes_id%8)*4));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x000001d0+hss_id*4), temp));
    CTC_ERROR_RETURN(_sys_greatbelt_chip_hss12g_prbs_test(lchip, hss_id, lane_idx));

    /* release gmac */
    gmac_id = serdes_id;

    field_value = 0;
    tb_id = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetPcs0_f + gmac_id;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
    tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
    tb_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

    field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* check codeerror */
    tb_id = QuadPcsPcs0ErrCnt0_t + (serdes_id/4) + (serdes_id%4)*36;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);

    while (failed_cnt < SYS_CHIP_SWITH_THREADHOLD)
    {
        /* delay 20 ms */
        sal_task_sleep(100);

        for (index = 0; index < 3; index++)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &errcnt));
        }

        if (0 == errcnt.pcs0_code_err_cnt)
        {
            /* switch success */
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "switch to sgmii success, serdes_idx:0x%x\n", serdes_id);
            break;
        }
        else
        {
            /* switch failed */
            failed_cnt++;
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "switch to sgmii failed, serdes_idx:0x%x failed_times:0x%x\n", serdes_id, failed_cnt);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "dynamic debug info: try switch times:%d \n", failed_cnt);

            /* switch AuxClkSel to original */
            /* AuxClkSelCfg, core clock */
            if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
            {
                CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x268, &temp));
                temp |= (1 << serdes_id);
                CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, temp));
            }
            /* AuxClkSelCfg, inf clock */
            CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x26c, &temp));
            temp |= (1 << serdes_id);
            CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, temp));

            /* Configure the HSS Lane to original rate(TX/RX) */
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, ori_tx));
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, ori_rx));

            /* try to switch again */
            if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
            {
                CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip , 0x268, &temp));
                temp &= ~(1 << serdes_id);
                CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, temp));
            }

            CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x26c, &temp));
            temp &= ~(1 << serdes_id);
            CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, temp));

            if (auxsel_used)
            {
                if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
                {
                    cfg_tmp = 0x7;
                    cfg_tmp = (cfg_tmp & HSS_INTERNEL_LANE_TX_REG0_MASK ) | cfg_tmp;
                    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));

                    cfg_tmp = 0x47;
                    cfg_tmp = (cfg_tmp & HSS_INTERNEL_LANE_RX_REG0_MASK ) | cfg_tmp;
                    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
                }
                else
                {
                    cfg_tmp = 0x27;
                    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
                    cfg_tmp = 0xc7;
                    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
                }
            }
            else
            {
                /* Configure the HSS Lane to 1G rate(TX/RX) */
                cfg_tmp = 0x7;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
                cfg_tmp = 0x47;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
            }
        }
    }

    /* Configure the HSS Lane to disable loopback mode and forceSignalDetect */
    enable = 0;
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_loopback_en(lchip, serdes_id, enable));

    tb_id = QuadPcsPcs0Cfg0_t + (serdes_id/4) + (serdes_id%4)*36;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quadpcscfg));
    quadpcscfg.pcs0_force_signal_detect = 0;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &quadpcscfg));

    /* reset gmac */
    field_value = 1;
    tb_id = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetPcs0_f + gmac_id;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 1;
    tbl_step1 = QuadPcsPcs1SoftRst0_t - QuadPcsPcs0SoftRst0_t;
    tbl_step2 = QuadPcsPcs0SoftRst1_t - QuadPcsPcs0SoftRst0_t;
    tb_id = QuadPcsPcs0SoftRst0_t + (gmac_id % 4) * tbl_step1 + (gmac_id / 4) * tbl_step2;

    field_id = QuadPcsPcs0SoftRst_Pcs0TxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_id = QuadPcsPcs0SoftRst_Pcs0RxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    if (failed_cnt >= SYS_CHIP_SWITH_THREADHOLD)
    {
        /* switch fail */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "switch to SGMII failed, need to switch to original mode!\n");
        return CTC_E_CHIP_SWITCH_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_to_qsgmii(uint8 lchip,uint8 serdes_id, uint8 lane_idx)
{
    uint32 tb_id = 0;
    uint32 cmd = 0;
    qsgmii_pcs_cfg_t qsgmiicfg;
    qsgmii_pcs_code_err_cnt_t errcnt;
    uint8 enable = 0;
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    uint8 qsgmii_id = 0;
    uint16 cfg_tmp = 0;
    uint32 temp = 0;
    uint8 hss_id = serdes_id/8;
    uint8 failed_cnt = 0;
    uint8 index = 0;

    _sys_greatbelt_chip_get_qsgmii_from_serdes(lchip, serdes_id, &qsgmii_id);

    /* Configure the HSS Lane to loopback mode */
    enable = 1;
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_loopback_en(lchip, serdes_id, enable));

    /* Configure the force signal detec of QsgmiiPcs */
    tb_id = QsgmiiPcsCfg0_t + qsgmii_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qsgmiicfg));
    qsgmiicfg.force_signal_detect = 1;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qsgmiicfg));

    /* Configure the HSS Lane to 5G rate(TX/RX) */
    cfg_tmp = 0x5;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
    cfg_tmp = 0x45;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));

    /* Configure the AuxClkSelCfg */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x268, &temp));
    temp |= (1 << serdes_id);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, temp));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x26c, &temp));
    temp |= (1 << serdes_id);
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, temp));

    /* Configure the QsgmiiHssSelCfg(cfgQsgmiiDis/cfgQuad*SgmiiTxSel both = 0) */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x3c0, &temp));
    temp &= ~(1 << (qsgmii_id+8));
    temp &= ~(1 << (qsgmii_id+16));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x3c0, temp));

    /* Configure the HssModeCfg */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip, (0x000001d0+hss_id*4), &temp));
    temp &= ~(0xf << ((serdes_id%8)*4));
    temp |= (0x1 << ((serdes_id%8)*4));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, (0x000001d0+hss_id*4), temp));
    CTC_ERROR_RETURN(_sys_greatbelt_chip_hss12g_prbs_test(lchip, hss_id, lane_idx));

    /* check codeerror */
    tb_id = QsgmiiPcsCodeErrCnt0_t + qsgmii_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);

    while (failed_cnt < SYS_CHIP_SWITH_THREADHOLD)
    {
        /* delay 20 ms */
        sal_task_sleep(100);

        for (index = 0; index < 3; index++)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &errcnt));
        }

        if (0 == errcnt.code_err_cnt)
        {
            /* switch success */
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "switch to sgmii success, serdes_idx:0x%x\n", serdes_id);
            break;
        }
        else
        {
            /* switch failed */
            failed_cnt++;
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "switch to qsgmii failed, serdes_idx:0x%x failed_times:0x%x\n", serdes_id, failed_cnt);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "dynamic debug info: try switch times:%d \n", failed_cnt);

            /* switch AuxClkSel to sgmii */
            CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x268, &temp));
            temp &= ~(1 << serdes_id);
            CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, temp));

            CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x26c, &temp));
            temp &= ~(1 << serdes_id);
            CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, temp));

            /* Configure the HSS Lane to 1G rate(TX/RX) */
            cfg_tmp = 0x7;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
            cfg_tmp = 0x47;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));

            /* switch AuxClkSel to 1g */
            CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x268, &temp));
            temp |= (1 << serdes_id);
            CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x268, temp));

            CTC_ERROR_RETURN(drv_greatbelt_chip_read(lchip ,0x26c, &temp));
            temp |= (1 << serdes_id);
            CTC_ERROR_RETURN(drv_greatbelt_chip_write(lchip, 0x26c, temp));

            /* Configure the HSS Lane to 1G rate(TX/RX) */
            cfg_tmp = 0x5;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
            cfg_tmp = 0x45;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
        }
    }

    /* Configure the HSS Lane to disable loopback mode and forceSignalDetect */
    enable = 0;
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_loopback_en(lchip, serdes_id, enable));

    tb_id = QsgmiiPcsCfg0_t + qsgmii_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qsgmiicfg));
    qsgmiicfg.force_signal_detect = 0;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qsgmiicfg));


    if (failed_cnt >= SYS_CHIP_SWITH_THREADHOLD)
    {
        /* switch fail */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "switch to QSGMII failed, need to switch to original mode!\n");
        return CTC_E_CHIP_SWITCH_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_to_xsgmii(uint8 lchip,uint8 serdes_id, uint8 lane_idx, uint8 sgmac_id)
{
    uint8 loop_back_reg = 1;
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    uint16 cfg_tmp = 0;
    sgmac_pcs_err_cnt_t error_cnt;
    sgmac_pcs_cfg_t sgmac_pcs_cfg;
    sgmac_mode_cfg_t sgmac_mode;
    sgmac_cfg_t sgmac_cfg;
    uint8 hss_id = serdes_id/8;
    uint32 cmd = 0;
    uint8 auxsel_used = 0;
    bool with_qsgmii = FALSE;
    uint32 tb_id = 0;
    uint8 failed_cnt = 0;
    uint32 value_tmp = 0;
    uint8 index = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    drv_datapath_serdes_info_t serdes_info;
    hss12_g0_cfg_t g0_cfg;
    hss12_g1_cfg_t g1_cfg;
    hss12_g2_cfg_t g2_cfg;
    hss12_g3_cfg_t g3_cfg;
    sal_memset(&g0_cfg, 0, sizeof(hss12_g0_cfg_t));
    sal_memset(&g1_cfg, 0, sizeof(hss12_g1_cfg_t));
    sal_memset(&g2_cfg, 0, sizeof(hss12_g2_cfg_t));
    sal_memset(&g3_cfg, 0, sizeof(hss12_g3_cfg_t));

CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                        (void*)&serdes_info));

switch(hss_id)
 {
     case 0:
         cmd = DRV_IOR(Hss12G0Cfg_t, DRV_ENTRY_FLAG);
         CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g0_cfg));
         auxsel_used = g0_cfg.cfg0_h_s_s_e_x_t_c16_s_e_l;
         break;
     case 1:
         cmd = DRV_IOR(Hss12G1Cfg_t, DRV_ENTRY_FLAG);
         CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g1_cfg));
         auxsel_used = g1_cfg.cfg1_h_s_s_e_x_t_c16_s_e_l;
         break;
     case 2:
         cmd = DRV_IOR(Hss12G2Cfg_t, DRV_ENTRY_FLAG);
         CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g2_cfg));
         auxsel_used = g2_cfg.cfg2_h_s_s_e_x_t_c16_s_e_l;
         break;
     case 3:
         cmd = DRV_IOR(Hss12G3Cfg_t, DRV_ENTRY_FLAG);
         CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g3_cfg));
         auxsel_used = g3_cfg.cfg3_h_s_s_e_x_t_c16_s_e_l;
         break;
     default:
         return CTC_E_INVALID_PARAM;
 }


    /*1. Config Hss Lane to loopback mode */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, &cfg_tmp));
    cfg_tmp |= 0x60;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, cfg_tmp));

    /*2. Config the force signel detec of Sgmac */
    tb_id = SgmacPcsCfg0_t + sgmac_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_pcs_cfg));
    sgmac_pcs_cfg.force_signal_detect = 1;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_pcs_cfg));

    with_qsgmii = with_qsgmii;
    with_qsgmii = _sys_greatbelt_chip_mix_mode_with_qsgmii(lchip, serdes_id);

/* Configure the HSS Lane to 1G rate(TX/RX) */
if (auxsel_used)
{
    if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
    {
        cfg_tmp = 0x7;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
        cfg_tmp = 0x47;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
    }
    else
    {
        cfg_tmp = 0x27;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
        cfg_tmp = 0xc7;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
    }
}
else
{
    cfg_tmp = 0x7;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
    cfg_tmp = 0x47;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
}

    /*4. Config the AuxClkSelCfg */
    if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
    {
        drv_greatbelt_chip_read(lchip, 0x00000268, &value_tmp);
        value_tmp &= ~(1 << serdes_id);
        drv_greatbelt_chip_write(lchip, 0x00000268, value_tmp);
    }

    drv_greatbelt_chip_read(lchip, 0x0000026c, &value_tmp);
    value_tmp &= ~(1<<serdes_id);
    drv_greatbelt_chip_write(lchip, 0x0000026c, value_tmp);

    /*5. Config SgmacModeCfg */
    cmd = DRV_IOR(SgmacModeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));
    sgmac_mode.cfg_sgmac_xfi_dis |= (1 << sgmac_id);
    cmd = DRV_IOW(SgmacModeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));

    /*6. Config Sgmac PcsMode to Sgmii */
    tb_id = SgmacCfg0_t + sgmac_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
    sgmac_cfg.pcs_mode = 1;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));

    /*7. Config the HssLane Mode */
    drv_greatbelt_chip_read(lchip, (0x000001d0+hss_id*4), &value_tmp);
    value_tmp &= ~(0xf << ((serdes_id%8)*4));
    value_tmp |= (0x3 << ((serdes_id%8)*4));
    drv_greatbelt_chip_write(lchip, (0x000001d0+hss_id*4), value_tmp);

    CTC_ERROR_RETURN(_sys_greatbelt_chip_hss12g_prbs_test(lchip, hss_id, lane_idx));

    /* release sgmac */
    field_value = 0;
    tb_id = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetSgmac0_f + (2*sgmac_id);
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 0;
    tb_id = SgmacSoftRst0_t + sgmac_id;

    field_id = SgmacSoftRst_PcsRxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_id = SgmacSoftRst_PcsTxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    /*8. Check CodeErrorCnt */
    tb_id = SgmacPcsErrCnt0_t + sgmac_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);

    while (failed_cnt < SYS_CHIP_SWITH_THREADHOLD)
    {
        /* delay 20 ms */
        sal_task_sleep(100);

        for (index = 0; index < 3; index++)
        {
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &error_cnt));
        }

        if (0 == error_cnt.code_err_cnt0)
        {
            /* switch success */
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "switch to 1g success, serdes_idx:0x%x\n", serdes_id);
            break;
        }
        else
        {
            /* switch failed */
            failed_cnt++;
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "switch to 1g failed, serdes_idx:0x%x failed_times:0x%x\n", serdes_id, failed_cnt);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "dynamic debug info: try switch times:%d \n", failed_cnt);
            /* switch AuxClkSel back to 10g */
            if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
            {
                drv_greatbelt_chip_read(lchip, 0x00000268, &value_tmp);
                value_tmp |= 1<<serdes_id;
                drv_greatbelt_chip_write(lchip, 0x00000268, value_tmp);
            }

            drv_greatbelt_chip_read(lchip, 0x0000026c, &value_tmp);
            value_tmp |= 1<<serdes_id;
            drv_greatbelt_chip_write(lchip, 0x0000026c, value_tmp);

            /* switch AuxClkSel to 1g */
            if (DRV_SERDES_QSGMII_MODE == serdes_info.mode)
            {
                drv_greatbelt_chip_read(lchip, 0x00000268, &value_tmp);
                value_tmp &= ~(1<<serdes_id);
                drv_greatbelt_chip_write(lchip, 0x00000268, value_tmp);
            }
            drv_greatbelt_chip_read(lchip, 0x0000026c, &value_tmp);
            value_tmp &= ~(1<<serdes_id);
            drv_greatbelt_chip_write(lchip, 0x0000026c, value_tmp);

        }
    }

    /* disable loopback mode */
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, &cfg_tmp));
    cfg_tmp &= 0x9f;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, cfg_tmp));

    /* disable forceSignalDetect */
    tb_id = SgmacPcsCfg0_t + sgmac_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_pcs_cfg));
    sgmac_pcs_cfg.force_signal_detect = 0;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_pcs_cfg));

    /* reset sgmac */
    field_value = 1;
    tb_id = ResetIntRelated_t;
    field_id = ResetIntRelated_ResetSgmac0_f + (2*sgmac_id);
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_value = 1;
    tb_id = SgmacSoftRst0_t + sgmac_id;

    field_id = SgmacSoftRst_PcsRxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    field_id = SgmacSoftRst_PcsTxSoftRst_f;
    cmd = DRV_IOW(tb_id, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


    if (failed_cnt >= SYS_CHIP_SWITH_THREADHOLD)
    {
        /* switch fail */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "switch to XSGMII failed, need to switch to original mode!\n");
        return CTC_E_CHIP_SWITCH_FAILED;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_to_xfi(uint8 lchip, uint8 serdes_idx, uint8 lane_idx, uint8 sgmac_id)
{
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    uint8 hss_id = serdes_idx/8;
    uint16 cfg_tmp = 0;
    uint32 value_tmp = 0;
    uint32 cmd = 0;
    sgmac_mode_cfg_t sgmac_mode;
    sgmac_cfg_t sgmac_cfg;
    uint32 tb_id = 0;
    uint32 field_id = 0;
    uint32 field_value = 0;

   /* 1. config hss lane to 10g */
    cfg_tmp = 0x04;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0, cfg_tmp));
    cfg_tmp = 0x0c;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0, cfg_tmp));

    /*2. Config the AuxClkSelCfg , 0x268:core for mac, 0x26c:inf for serdes;  1:not use aux clock, 0:use aux clock*/
#if 0
    drv_greatbelt_chip_read(lchip, 0x00000268, &value_tmp);
    value_tmp |= (1<<serdes_idx);
    drv_greatbelt_chip_write(lchip, 0x00000268, value_tmp);
#endif


    drv_greatbelt_chip_read(lchip, 0x0000026c, &value_tmp);
    value_tmp |= (1<<serdes_idx);
    drv_greatbelt_chip_write(lchip, 0x0000026c, value_tmp);



    /*3. Config SgmacModeCfg */
    cmd = DRV_IOR(SgmacModeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));
    sgmac_mode.cfg_sgmac_xfi_dis &= ~(1 << sgmac_id);
    cmd = DRV_IOW(SgmacModeCfg_t, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));

    /*4. Config Sgmac PcsMode to xfi */
    tb_id = SgmacCfg0_t + sgmac_id;
    cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
    sgmac_cfg.pcs_mode = 2;
    cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));

    /*7. Config the HssLane Mode */
    drv_greatbelt_chip_read(lchip, (0x000001d0+hss_id*4), &value_tmp);
    value_tmp &= ~(0xf << ((serdes_idx%8)*4));
    value_tmp |= (0x4 << ((serdes_idx%8)*4));
    drv_greatbelt_chip_write(lchip, (0x000001d0+hss_id*4), value_tmp);

    /* check whether clockgate for xfi is enable*/
    field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Xfi_f + 2 * (sgmac_id);
    field_value = 1;
    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

STATIC bool
_sys_greatbelt_chip_switch_check_confilict(uint8 lchip, uint8 serdes_idx, uint8 ori_mode, uint8 switch_mode)
{
    uint8 hss_id = serdes_idx/8;
    uint8 index = 0;
    uint8 sgmac_mapping[8] = {8, 9, 10, 11, 6, 7, 4, 5};
    uint8 qsgmii_id = 0;
    uint8 lport = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    /* just for testing */
    return FALSE;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Serdes infor: serdes_idx=%d, ori_mode=%d, switch_mode=%d\n",
       serdes_idx, ori_mode, switch_mode);
    /*
        xsgmii switch to xaui, mapping:
        hss 0:         xsgmii (56,58,NA,NA,57,59,NA,NA) ------------xaui(8, 9)
        hss 1:         xsgmii (58,NA,NA,NA,59,NA,NA,NA) ------------xaui(10, 11)
        hss 2:         xsgmii (54,NA,56,57,55,NA,58,59) ------------xaui(6, 7)
        hss 3:         xsgmii (48,49,50,51,52,53,54,55) ------------xaui(4, 5)
    */
    if (DRV_SERDES_XAUI_MODE == switch_mode)
    {
        if (DRV_SERDES_XGSGMII_MODE == ori_mode)
        {
            return FALSE;
        }

        for (index = 0; index < 2; index++)
        {
            /*
            ret = drv_greatbelt_get_sgmac_info(lchip, sgmac_mapping[hss_id*2+index], DRV_CHIP_MAC_PCS_INFO, &temp);
            */
            lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, (sgmac_mapping[hss_id*2+index] + SYS_MAX_GMAC_PORT_NUM));
            drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
            if (port_cap.valid)
            {
                return TRUE;
            }
        }
    }
    else if (DRV_SERDES_XGSGMII_MODE == switch_mode)
    {
        /* only xaui to xsgmii need check */
        if (DRV_SERDES_XAUI_MODE == ori_mode)
        {
            switch (hss_id)
            {
                case 0:
                    for (index = 56; index <= 59; index++)
                    {
                         /*ret = drv_greatbelt_get_sgmac_info(lchip, (index-48), DRV_CHIP_MAC_PCS_INFO, &temp);*/
                        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, index);
                        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

                        if (port_cap.valid)
                        {
                            return TRUE;
                        }
                    }
                    break;
                case 1:
                    for (index = 58; index <= 59; index++)
                    {
                         /*ret = drv_greatbelt_get_sgmac_info(lchip, (index-SYS_MAX_GMAC_PORT_NUM), DRV_CHIP_MAC_PCS_INFO, &temp);*/
                        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, index);
                        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

                        if (port_cap.valid)
                        {
                            return TRUE;
                        }
                    }
                    break;
                case 2:
                    for (index = 54; index <= 59; index++)
                    {
                         /*ret = drv_greatbelt_get_sgmac_info(lchip, (index-48), DRV_CHIP_MAC_PCS_INFO, &temp);*/
                        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, index);
                        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

                        if (port_cap.valid)
                        {
                            return TRUE;
                        }
                    }
                    break;
                case 3:
                    for (index = 48; index <= 55; index++)
                    {
                         /*ret = drv_greatbelt_get_sgmac_info(lchip, (index-48), DRV_CHIP_MAC_PCS_INFO, &temp);*/
                        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, index);
                        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

                        if (port_cap.valid)
                        {
                            return TRUE;
                        }
                    }
                    break;
                default:
                    break;
            }
        }
    }
    else if (DRV_SERDES_SGMII_MODE == switch_mode)
    {
        /* from xaui to sgmii, per hss */
        if (DRV_SERDES_XAUI_MODE == ori_mode)
        {
            for (index = hss_id*8; index < hss_id*8+8; index++)
            {
                 /*ret = drv_greatbelt_get_gmac_info(lchip, index, DRV_CHIP_MAC_PCS_INFO, &temp);*/
                lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, index);
                drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

                if (port_cap.valid)
                {
                    return TRUE;
                }
            }
        }
        else
        {
            if (DRV_SERDES_QSGMII_MODE == ori_mode)
            {
                _sys_greatbelt_chip_get_qsgmii_from_serdes(lchip, serdes_idx, &qsgmii_id);
                if ((serdes_idx >=qsgmii_id*4) && (serdes_idx <= qsgmii_id*4+4))
                {
                    return FALSE;
                }
            }
            /* switch to sgmii per lane */
             /*ret = drv_greatbelt_get_gmac_info(lchip, serdes_idx, DRV_CHIP_MAC_PCS_INFO, &temp);*/
            lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, serdes_idx);
            drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

            if (port_cap.valid)
            {
                return TRUE;
            }
        }
    }
    else if (DRV_SERDES_QSGMII_MODE == switch_mode)
    {
        return FALSE;

        _sys_greatbelt_chip_get_qsgmii_from_serdes(lchip, serdes_idx, &qsgmii_id);
        for (index = 0; index < 4; index++)
        {
             /*ret = drv_greatbelt_get_gmac_info(lchip, qsgmii_id*4, DRV_CHIP_MAC_PCS_INFO, &temp);*/
            lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, qsgmii_id*4);
            drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

            if (port_cap.valid)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

STATIC int32
_sys_greatbelt_chip_switch_xsgmii_to_xfi(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 lane_idx = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 sgmac_id = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                        (void*)&serdes_info));

    lane_idx = serdes_info.lane_idx;
    sgmac_id = serdes_info.mac_id - SYS_MAX_GMAC_PORT_NUM;

    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_xfi(lchip,serdes_idx, lane_idx, sgmac_id));

    pcs_mode = DRV_SERDES_XFI_MODE;
    speed_mode = DRV_SERDES_SPPED_10DOT3125G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_idx, 1);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);

    sys_greatbelt_port_switch_dest_port(lchip, lport_valid, serdes_info.mac_id, dest_lport);
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_sgmii_to_xfi(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hss_id = 0;
    uint8 lane_idx = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 sgmac_id = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 sgmac_map[32] =
    {
        0x08,  0x0a,  0xff,  0xff,  0x09,  0x0b,  0xff,  0xff,
        0x0a,  0xff,  0xff,  0xff,  0x0b,  0xff,  0xff,  0xff,
        0x06,  0xff,  0x08,  0x09,  0x07,  0xff,  0x0a,  0x0b,
        0x00,  0x01,  0x02,  0x03,  0x04,  0x05,  0x06,  0x07
    };
    uint8 chan_id = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                        (void*)&serdes_info));

    hss_id = serdes_info.hss_id;
    lane_idx = serdes_info.lane_idx;

    sgmac_id = sgmac_map[serdes_idx];
    if (sgmac_id >= SYS_MAX_SGMAC_PORT_NUM)
    {
        return CTC_E_INVALID_PORT_MAC_TYPE;
    }

    /*if (FALSE == SYS_MAC_IS_VALID(lchip, (sgmac_id+SYS_MAX_GMAC_PORT_NUM)))
    {
        return CTC_E_MAC_NOT_USED;
    }*/


    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_xfi(lchip, serdes_idx, lane_idx, sgmac_id));
    CTC_ERROR_RETURN(_sys_greatbelt_chip_select_sgmac_hss(lchip, sgmac_id, hss_id));

    sgmac_id += SYS_MAX_GMAC_PORT_NUM;
    pcs_mode = DRV_SERDES_XFI_MODE;
    speed_mode = DRV_SERDES_SPPED_10DOT3125G;

    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_idx, 1);
    CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, sgmac_id, &chan_id));
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_MACID_INFO, (void*)&sgmac_id);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id);
    sys_greatbelt_port_switch_dest_port(lchip, lport_valid, sgmac_id, dest_lport);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_xfi_to_xsgmii(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 lane_idx = 0;
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint32 field_value = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 sgmac_id = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Serdes infor: hssid=%d, lane_idx=%d, mac_id=%d\n",
        serdes_info.hss_id, serdes_info.lane_idx, serdes_info.mac_id);

    lane_idx = serdes_info.lane_idx;

    sgmac_id = serdes_info.mac_id - SYS_MAX_GMAC_PORT_NUM;

    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_xsgmii(lchip, serdes_idx, lane_idx, sgmac_id));

    /* check whether clockgate for xgsgmii is enable*/
    field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f + 2 * (sgmac_id);
    field_value = 1;
    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* update soft table */
    pcs_mode = DRV_SERDES_XGSGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_idx, 1);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);

    sys_greatbelt_port_switch_dest_port(lchip, lport_valid, serdes_info.mac_id, dest_lport);

    /*wait mac sync up */
    sal_task_sleep(100);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_xaui_to_xsgmii(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hssid = serdes_idx/8;
    uint32 tb_id = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 cmd = 0;
    uint8 temp = 0;
    uint8 valid_port = 0;
    uint8 sgmac_id = 0;
    uint8 chan_id = 0;
    uint8 pcs_mode = 0;
    uint8 serdes_info = 0;
    uint8 speed_mode  = 0;
    uint8 gchip = 0;
    uint32 enable = 0;
    sgmac_mode_cfg_t sgmac_mode;
    sgmac_cfg_t sgmac_cfg;
    sgmac_soft_rst_t sgmac_rst;
    /* 8: lane index in one HSS */
    uint8 sgmac_mapping[HSS_MACRO_NUM][8] =
    {
        {56, 58,    0xff, 0xff, 57, 59,    0xff, 0xff},
        {58, 0xff, 0xff, 0xff, 59, 0xff, 0xff, 0xff},
        {54, 0xff, 56,    57,    55, 0xff, 58,    59},
        {48, 49,    50,    51,    52,  53,   54,     55}
    };
    uint8 lport = 0;
    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
    sal_memset(&sgmac_mode, 0, sizeof(sgmac_mode_cfg_t));

    if (hssid >= HSS_MACRO_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    for (temp = 0; temp < 8; temp++)
    {
        sgmac_id =  sgmac_mapping[hssid][temp];
        if (0xff == sgmac_id)
        {
            continue;
        }

        /*if (FALSE == SYS_MAC_IS_VALID(lchip, sgmac_id))
        {
            return CTC_E_MAC_NOT_USED;
        }*/
    }

    /* set hss macro to xsgmii mode */
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_macro_mode(lchip, hssid, DRV_SERDES_XGSGMII_MODE));

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    /* Config mac PcsMode to xsgmii and reset orignal XAUI */
    for (temp = 0; temp < 8; temp++)
    {
        sgmac_id =  sgmac_mapping[hssid][temp];
        if (0xff == sgmac_id)
        {
            continue;
        }

        tb_id = SgmacSoftRst0_t + (sgmac_id-48);
        sgmac_rst.pcs_rx_soft_rst = 1;
        sgmac_rst.pcs_tx_soft_rst = 1;
        sgmac_rst.serdes_rx0_soft_rst = 1;
        sgmac_rst.serdes_rx1_soft_rst = 1;
        sgmac_rst.serdes_rx2_soft_rst = 1;
        sgmac_rst.serdes_rx3_soft_rst = 1;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));

        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, sgmac_id);
        sys_greatbelt_port_get_mac_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, lport), &enable);
        if (enable)
        {
            tb_id = SgmacCfg0_t + (sgmac_id-SYS_MAX_GMAC_PORT_NUM);
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
            sgmac_cfg.pcs_mode = 1;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));

            tb_id = SgmacSoftRst0_t + (sgmac_id-SYS_MAX_GMAC_PORT_NUM);
            field_value = 0;
            cmd = DRV_IOW(tb_id, SgmacSoftRst_PcsRxSoftRst_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            cmd = DRV_IOW(tb_id, SgmacSoftRst_PcsTxSoftRst_f);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
        }

#if 0
        tb_id = SgmacCfg0_t + (sgmac_id-SYS_MAX_GMAC_PORT_NUM);
        cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
        sgmac_cfg.pcs_mode = 1;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
#endif

        /* check whether clockgate for xsgmii is enable*/
        field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f+(sgmac_id-SYS_MAX_GMAC_PORT_NUM)*2;
        field_value = 1;
        cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

#if 0
        tb_id = SgmacSoftRst0_t + (sgmac_id-SYS_MAX_GMAC_PORT_NUM);
        sgmac_rst.pcs_rx_soft_rst = 1;
        sgmac_rst.pcs_tx_soft_rst = 1;
        sgmac_rst.serdes_rx0_soft_rst = 1;
        sgmac_rst.serdes_rx1_soft_rst = 1;
        sgmac_rst.serdes_rx2_soft_rst = 1;
        sgmac_rst.serdes_rx3_soft_rst = 1;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));

        tb_id = SgmacSoftRst0_t + (sgmac_id-48);
        field_value = 0;
        cmd = DRV_IOW(tb_id, SgmacSoftRst_PcsRxSoftRst_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

        cmd = DRV_IOW(tb_id, SgmacSoftRst_PcsTxSoftRst_f);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
#endif
        /*5. Config SgmacModeCfg */
        cmd = DRV_IOR(SgmacModeCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));
        sgmac_mode.cfg_sgmac_xfi_dis |= (1 << ((sgmac_id-SYS_MAX_GMAC_PORT_NUM)));
        cmd = DRV_IOW(SgmacModeCfg_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_mode));

        CTC_ERROR_RETURN(_sys_greatbelt_chip_select_sgmac_hss(lchip, (sgmac_id-SYS_MAX_GMAC_PORT_NUM), hssid));
    }


    /* update software */
    temp = DRV_SERDES_XGSGMII_MODE;
    CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_MODE_INFO, (void*)&temp));

    if (hssid == 0)
    {
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC10_INFO, (void*)&hssid ));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC11_INFO, (void*)&hssid ));
    }
    else if (hssid == 2)
    {
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC8_INFO, (void*)&hssid ));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC9_INFO, (void*)&hssid ));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC10_INFO, (void*)&hssid ));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC11_INFO, (void*)&hssid ));
    }
    else if (hssid == 3)
    {
        /* no need to care xsgmii 48-51, becasuse they are always on HSS-3 */
        temp = 0;
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC4_INFO, (void*)&temp));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC5_INFO, (void*)&temp));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC6_INFO, (void*)&hssid ));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC7_INFO, (void*)&hssid ));
    }

    pcs_mode = DRV_SERDES_XGSGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, hssid*8, 8);
    for (temp = 0; temp < 8; temp++)
    {
        sgmac_id =  sgmac_mapping[hssid][temp];
        if (0xff == sgmac_id)
        {
            continue;
        }
        serdes_info = hssid*8+temp;

        CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, sgmac_id, &chan_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_MACID_INFO, (void*)&sgmac_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id));
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, sgmac_id, dest_lport+valid_port);
        valid_port++;
    }

    /*wait mac sync up */
    sal_task_sleep(100);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_xsgmii_to_xaui(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hssid = serdes_idx/8;
    uint32 tb_id = 0;
    uint32 cmd = 0;
    uint8 sgmac_id = 0;
    sgmac_cfg_t sgmac_cfg;
    sgmac_soft_rst_t sgmac_rst;
    uint32 field_id = 0;
    uint32 field_value = 0;
    uint8 index = 0;
    uint8 pcs_mode = 0;
    uint8 temp = 0;
    uint8 speed_mode = 0;
    uint8 xaui_mapping[8] = {8, 9, 10, 11, 6, 7, 4, 5};
    uint8 gchip = 0;
    uint32 enable = 0;
    uint8 lport = 0;
    uint8 chan_id = 0;
    sal_memset(&sgmac_rst, 0, sizeof(sgmac_soft_rst_t));

    if (hssid >= HSS_MACRO_NUM)
    {
        return CTC_E_INVALID_PARAM;
    }

    /*for (index = 0; index < 2; index++)
    {
        sgmac_id = xaui_mapping[hssid*2+index] + SYS_MAX_GMAC_PORT_NUM;
        if (FALSE == SYS_MAC_IS_VALID(lchip, sgmac_id))
        {
            return CTC_E_MAC_NOT_USED;
        }
    }*/

    /* check confilict */
    if (TRUE == _sys_greatbelt_chip_switch_check_confilict(lchip, serdes_idx, DRV_SERDES_XGSGMII_MODE, DRV_SERDES_XAUI_MODE))
    {
        return CTC_E_GLOBAL_CONFIG_CONFLICT;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "_sys_greatbelt_chip_switch_xsgmii_to_xaui, serdes_id:%d, hssid;%d\n", serdes_idx, hssid);

    for (index = 0; index < 2; index++)
    {
        sgmac_id = xaui_mapping[hssid*2+index];

        /* reset whole softrst */
        tb_id = SgmacSoftRst0_t + sgmac_id;
        sgmac_rst.pcs_rx_soft_rst = 1;
        sgmac_rst.pcs_tx_soft_rst = 1;
        sgmac_rst.serdes_rx0_soft_rst = 1;
        sgmac_rst.serdes_rx1_soft_rst = 1;
        sgmac_rst.serdes_rx2_soft_rst = 1;
        sgmac_rst.serdes_rx3_soft_rst = 1;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
    }

    /* set hss macro to xaui mode */
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_macro_mode(lchip, hssid, DRV_SERDES_XAUI_MODE));

    sal_task_sleep(10);

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    /* Config Sgmac PcsMode to xaui */
    for (temp = 0; temp < 2; temp++)
    {
        sgmac_id =  xaui_mapping[hssid*2+temp];
        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, (sgmac_id+SYS_MAX_GMAC_PORT_NUM));
        sys_greatbelt_port_get_mac_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, lport), &enable);
        if (enable)
        {
            tb_id = SgmacCfg0_t + sgmac_id;

            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
            sgmac_cfg.pcs_mode = 3;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));

            field_value = 0;

            tb_id = SgmacSoftRst0_t + sgmac_id;
            field_id = SgmacSoftRst_PcsTxSoftRst_f;
            cmd = DRV_IOW(tb_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            field_id = SgmacSoftRst_PcsRxSoftRst_f;
            cmd = DRV_IOW(tb_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            for (index = 0; index < 4; index++)
            {
                field_id = SgmacSoftRst_SerdesRx0SoftRst_f + index;
                cmd = DRV_IOW(tb_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            }
        }
    }

    /* update software */
    temp = DRV_SERDES_XAUI_MODE;
    CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_MODE_INFO, (void*)&temp));

    if (hssid == 3)
    {
        temp = 1;
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC4_INFO, (void*)&temp));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC5_INFO, (void*)&temp));
    }

    pcs_mode = DRV_SERDES_XAUI_MODE;
    speed_mode = DRV_SERDES_SPPED_3DOT125G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, hssid*8, 8);
    /* set all 8 serdes in one HSS to XAUI mode */
    for (index = 0; index < 8; index++)
    {
        sgmac_id = xaui_mapping[hssid*2+index/4] + SYS_MAX_GMAC_PORT_NUM;

        CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, sgmac_id, &chan_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_MACID_INFO, (void*)&sgmac_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id));
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, sgmac_id, dest_lport+index/4);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_sgmii_to_xaui(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hssid = serdes_idx/8;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 index = 0;
    uint8 sgmac_id = 0;
    uint8 sgmac_mapping[8] = {8, 9, 10, 11, 6, 7, 4, 5};
    uint8 chan_id = 0;
    uint8 sub_idx = 0;
    uint32 field_value = 0;
    uint32 tb_id = 0;
    sgmac_soft_rst_t sgmac_rst;
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint8 gchip = 0;
    uint32 enable = 0;
    uint8 lport = 0;
    sgmac_cfg_t sgmac_cfg;

    if (serdes_idx >= 24)
    {
        return CTC_E_INVALID_PARAM;
    }

    /*for (index = 0; index < 2; index++)
    {
        sgmac_id = sgmac_mapping[hssid*2+index] + SYS_MAX_GMAC_PORT_NUM;
        if (FALSE == SYS_MAC_IS_VALID(lchip, sgmac_id))
        {
            return CTC_E_MAC_NOT_USED;
        }
    }*/

    /* check confilict */
    if (TRUE == _sys_greatbelt_chip_switch_check_confilict(lchip, serdes_idx, DRV_SERDES_SGMII_MODE, DRV_SERDES_XAUI_MODE))
    {
        return CTC_E_GLOBAL_CONFIG_CONFLICT;
    }

    for (index = 0; index < 2; index++)
    {
        sgmac_id = sgmac_mapping[hssid*2+index];

        /* reset whole softrst */
        tb_id = SgmacSoftRst0_t + sgmac_id;
        sgmac_rst.pcs_rx_soft_rst = 1;
        sgmac_rst.pcs_tx_soft_rst = 1;
        sgmac_rst.serdes_rx0_soft_rst = 1;
        sgmac_rst.serdes_rx1_soft_rst = 1;
        sgmac_rst.serdes_rx2_soft_rst = 1;
        sgmac_rst.serdes_rx3_soft_rst = 1;
        cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_rst));
    }

    /* set hss macro to xaui mode */
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_macro_mode(lchip, hssid, DRV_SERDES_XAUI_MODE));

    sal_task_sleep(10);

    sys_greatbelt_get_gchip_id(lchip, &gchip);

     for (index = 0; index < 2; index++)
    {
        sgmac_id = sgmac_mapping[hssid*2+index];
        lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, (sgmac_id+SYS_MAX_GMAC_PORT_NUM));
        sys_greatbelt_port_get_mac_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, lport), &enable);
        if (enable)
        {
            tb_id = SgmacCfg0_t + sgmac_id;
            cmd = DRV_IOR(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));
            sgmac_cfg.pcs_mode = 3;
            cmd = DRV_IOW(tb_id, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &sgmac_cfg));


            field_value = 0;
            tb_id = SgmacSoftRst0_t + sgmac_id;
            field_id = SgmacSoftRst_PcsTxSoftRst_f;
            cmd = DRV_IOW(tb_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            field_id = SgmacSoftRst_PcsRxSoftRst_f;
            cmd = DRV_IOW(tb_id, field_id);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

            for (sub_idx = 0; sub_idx < 4; sub_idx++)
            {
                field_id = SgmacSoftRst_SerdesRx0SoftRst_f + sub_idx;
                cmd = DRV_IOW(tb_id, field_id);
                CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            }
        }
    }

    /* update software */
    pcs_mode = DRV_SERDES_XAUI_MODE;
    CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_MODE_INFO, (void*)&pcs_mode));
    if (hssid == 0)
    {
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC8_INFO, (void*)&hssid));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC9_INFO, (void*)&hssid));
    }
    else if (hssid == 1)
    {
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC10_INFO, (void*)&hssid));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC11_INFO, (void*)&hssid));
    }
    else if (hssid == 2)
    {
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC6_INFO, (void*)&hssid));
        CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_SGMAC7_INFO, (void*)&hssid));
    }


    speed_mode = DRV_SERDES_SPPED_3DOT125G;

    sys_greatbelt_port_switch_src_port(lchip, lport_valid, hssid*8, 8);
    for (index = 0; index < 8; index++)
    {
        sgmac_id = sgmac_mapping[hssid*2+index/4] + SYS_MAX_GMAC_PORT_NUM;
        CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, sgmac_id, &chan_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_MACID_INFO, (void*)&sgmac_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id));
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, sgmac_id, dest_lport+index/4);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_xaui_to_sgmii(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hssid = 0;
    uint8 temp = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 serdes_info = 0;
    uint8 chan_id = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (serdes_idx >= 24)
    {
        return CTC_E_INVALID_PARAM;
    }

    hssid = serdes_idx/8;

    /*for (temp = 0; temp < 8; temp++)
    {
        serdes_info  =  (hssid*8+temp);
        if (FALSE == SYS_MAC_IS_VALID(lchip, serdes_info))
        {
            return CTC_E_MAC_NOT_USED;
        }
    }*/

    /* check confilict */
    if (TRUE == _sys_greatbelt_chip_switch_check_confilict(lchip, serdes_idx, DRV_SERDES_XAUI_MODE, DRV_SERDES_SGMII_MODE))
    {
        return CTC_E_GLOBAL_CONFIG_CONFLICT;
    }

    /* set hss macro to sgmii mode */
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_macro_mode(lchip, hssid, DRV_SERDES_SGMII_MODE));

    /* update software */
    temp = DRV_SERDES_SGMII_MODE;
    CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_MODE_INFO, (void*)&temp));

    pcs_mode = DRV_SERDES_SGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, hssid*8, 8);
    /* set all 8 gmac in one HSS */
    for (temp = 0; temp < 8; temp++)
    {
        serdes_info  =  (hssid*8+temp);
        CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, serdes_info, &chan_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_MACID_INFO, (void*)&serdes_info));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id));
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, serdes_info, dest_lport+temp);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_2dot5_to_sgmii(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hssid = 0;
    uint8 temp = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 serdes_info = 0;
    uint8 chan_id = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    if (serdes_idx >= 24)
    {
        return CTC_E_INVALID_PARAM;
    }

    hssid = serdes_idx/8;

    /*for (temp = 0; temp < 8; temp++)
    {
        serdes_info  =  (hssid*8+temp);
        if (FALSE == SYS_MAC_IS_VALID(lchip, serdes_info))
        {
            return CTC_E_MAC_NOT_USED;
        }
    }*/

    /* set hss macro to sgmii mode */
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_macro_mode(lchip, hssid, DRV_SERDES_SGMII_MODE));

    /* update software */
    temp = DRV_SERDES_SGMII_MODE;
    CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_MODE_INFO, (void*)&temp));

    pcs_mode = DRV_SERDES_SGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, hssid*8, 8);
    /* set all 8 gmac in one HSS */
    for (temp = 0; temp < 8; temp++)
    {
        serdes_info  =  (hssid*8+temp);
        CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, serdes_info, &chan_id));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_MACID_INFO, (void*)&serdes_info));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, serdes_info, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id));
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, serdes_info, dest_lport+temp);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_sgmii_to_2dot5(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hssid = serdes_idx/8;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 index = 0;
    uint8 gchip = 0;

    if (serdes_idx >= 24)
    {
        return CTC_E_INVALID_PARAM;
    }

    /* set hss macro to xaui mode */
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_macro_mode(lchip, hssid, DRV_SERDES_2DOT5_MODE));

    sal_task_sleep(10);

    sys_greatbelt_get_gchip_id(lchip, &gchip);

    /* update software */
    pcs_mode = DRV_SERDES_2DOT5_MODE;
    CTC_ERROR_RETURN(drv_greatbelt_set_hss_info(lchip, hssid, DRV_CHIP_HSS_MODE_INFO, (void*)&pcs_mode));

    speed_mode = DRV_SERDES_SPPED_3DOT125G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, hssid*8, 8);
    for (index = 0; index < 8; index++)
    {
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode));
        CTC_ERROR_RETURN(drv_greatbelt_set_serdes_info(lchip, (hssid*8+index), DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode));
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, hssid*8+index, dest_lport+index);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_to_2dot5_mode(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    drv_datapath_serdes_info_t serdes_info;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    if (serdes_info.mode == DRV_SERDES_SGMII_MODE)
    {
        /* do switch from sgmii to 2.5g */
       CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_sgmii_to_2dot5(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else
    {
        /* not support */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes %d mode %d not support switch to xaui! \n", serdes_idx, serdes_info.mode);
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_xfi_to_sgmii(uint8 lchip, uint8 serdes_id, uint8 lport_valid, uint16 dest_lport)
{
    uint8 lane_idx = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 chan_id = 0;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 field_value = 0;
    uint8 hss_id = serdes_id/8;
    uint8 index = 0;
    uint8 is_have_xfi = 0;

    sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*if (FALSE == SYS_MAC_IS_VALID(lchip, serdes_id))
    {
        return CTC_E_MAC_NOT_USED;
    }*/

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                            (void*)&serdes_info));
    lane_idx = serdes_info.lane_idx;


    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_sgmii(lchip, serdes_id, lane_idx));

    pcs_mode = DRV_SERDES_SGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_id, 1);
    CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, serdes_id, &chan_id));
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MACID_INFO, (void*)&serdes_id);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id);

    sys_greatbelt_port_switch_dest_port(lchip, lport_valid, serdes_id, dest_lport);

    /* fix xfi jitter issue */
    for (index = hss_id*8; index < hss_id*8+8; index++)
    {
        drv_greatbelt_get_serdes_info(lchip, index, DRV_CHIP_SERDES_ALL_INFO,
                            (void*)&serdes_info);

        if (DRV_SERDES_XFI_MODE == serdes_info.mode)
        {
           is_have_xfi = 1;
           break;
        }
    }
    if (!is_have_xfi)
    {
        drv_greatbelt_chip_write_hss12g(lchip, (serdes_id/8), ((0x10<<6)|0x0a), 4);
    }

    /*disable dpc*/
    field_value = 0x7fff;
    drv_greatbelt_chip_write_hss12g(lchip, (serdes_id/8), ((rx_lane_addr[serdes_info.lane_idx]<<6)|0x1f), field_value);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_qsgmii_to_sgmii(uint8 lchip, uint8 serdes_id, uint8 lport_valid, uint16 dest_lport)
{
    uint8 lane_idx = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 chan_id = 0;
    uint8 index = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /*if (FALSE == SYS_MAC_IS_VALID(lchip, serdes_id))
    {
        return CTC_E_MAC_NOT_USED;
    }*/

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                            (void*)&serdes_info));
    lane_idx = serdes_info.lane_idx;

    /* check confilict */
    if (_sys_greatbelt_chip_switch_check_confilict(lchip, serdes_id,     \
                DRV_SERDES_QSGMII_MODE, DRV_SERDES_SGMII_MODE))
    {
        return CTC_E_GLOBAL_CONFIG_CONFLICT;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_sgmii(lchip, serdes_id, lane_idx));


    pcs_mode = DRV_SERDES_SGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_id, 1);
    CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, serdes_id, &chan_id));
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MACID_INFO, (void*)&serdes_id);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id);
    for (index=0; index<4; index++)
    {
        /* get serdes information */
        CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id+index, DRV_CHIP_SERDES_MODE_INFO,
                                    (void*)&pcs_mode));

        if (pcs_mode == DRV_SERDES_SGMII_MODE)
        {
            sys_greatbelt_port_switch_dest_port(lchip, lport_valid, serdes_id+index, dest_lport+index);
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_sgmii_to_qsgmii(uint8 lchip, uint8 serdes_id, uint8 lport_valid, uint16 dest_lport)
{
    uint8 qsgmii_id = 0;
    uint8 lane_idx = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 mac_id = 0;
    uint8 chan_id = 0;
    uint8 index = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* check confilict */
    CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_check_confilict(lchip, serdes_id,
            DRV_SERDES_SGMII_MODE, DRV_SERDES_QSGMII_MODE));

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                            (void*)&serdes_info));
    lane_idx = serdes_info.lane_idx;

    _sys_greatbelt_chip_get_qsgmii_from_serdes(lchip, serdes_id, &qsgmii_id);

    /*for (index=0; index<4; index++)
    {
        if (FALSE == SYS_MAC_IS_VALID(lchip, qsgmii_id*4+index))
        {
            return CTC_E_MAC_NOT_USED;
        }
    }*/

    if (0xff == qsgmii_id)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes %d  not support switch to qsgmii mode \n", serdes_id);
        return CTC_E_NOT_SUPPORT;
    }

    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_qsgmii(lchip, serdes_id, lane_idx));

    /* update software */
    pcs_mode = DRV_SERDES_QSGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_5G;
    mac_id = qsgmii_id*4;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_id, 1);
    CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, mac_id, &chan_id));
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MACID_INFO, (void*)&mac_id);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id);
    for (index=0; index<4; index++)
    {
        sys_greatbelt_port_switch_dest_port(lchip, lport_valid, mac_id+index, dest_lport+index);
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_qsgmii_to_xsgmii(uint8 lchip, uint8 serdes_id, uint8 lport_valid, uint16 dest_lport)
{
    uint8 hss_id = 0;
    uint8 lane_idx = 0;
    uint32 cmd = 0;
    uint32 field_id = 0;
    uint32 field_value = 0;
    drv_datapath_serdes_info_t serdes_info;
    uint8 sgmac_id = 0;
    uint8 chan_id = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;
    uint8 index = 0;

    /* 8: lane index in one HSS */
    uint8 sgmac_mapping[HSS_MACRO_NUM][8] =
    {
        {56, 58,    0xff, 0xff, 57, 59,    0xff, 0xff},
        {58, 0xff, 0xff, 0xff, 59, 0xff, 0xff, 0xff},
        {54, 0xff, 56,    57,    55, 0xff, 58,    59},
        {48, 49,    50,    51,    52,  53,   54,     55}
    };

    drv_datapath_port_capability_t port_cap;
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Serdes infor: hssid=%d, lane_idx=%d, mac_id=%d\n",
        serdes_info.hss_id, serdes_info.lane_idx, serdes_info.mac_id);

    hss_id = serdes_info.hss_id;
    lane_idx = serdes_info.lane_idx;

    if (0xff == sgmac_mapping[hss_id][lane_idx])
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Serdes %d not support dynamic switch to xsgmii! \n", serdes_id);
        return CTC_E_FEATURE_NOT_SUPPORT;
    }

    /*if (FALSE == SYS_MAC_IS_VALID(lchip, sgmac_mapping[hss_id][lane_idx]))
    {
        return CTC_E_MAC_NOT_USED;
    }*/

    sgmac_id = sgmac_mapping[hss_id][lane_idx] - SYS_MAX_GMAC_PORT_NUM;

    /* do switch */
    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_to_xsgmii(lchip, serdes_id, lane_idx, sgmac_id));
    CTC_ERROR_RETURN(_sys_greatbelt_chip_select_sgmac_hss(lchip, sgmac_id, hss_id));

    /* check whether clockgate for xgsgmii is enable */
    field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f + 2 * (sgmac_id);
    field_value = 1;
    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /* update soft table */
    pcs_mode = DRV_SERDES_XGSGMII_MODE;
    speed_mode = DRV_SERDES_SPPED_1DOT25G;
    sgmac_id += SYS_MAX_GMAC_PORT_NUM;
    sys_greatbelt_port_switch_src_port(lchip, lport_valid, serdes_id, 1);
    CTC_ERROR_RETURN(_sys_greatbelt_chip_get_chann_id_with_mac(lchip, sgmac_id, &chan_id));
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MODE_INFO, (void*)&pcs_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_SPEED_INFO, (void*)&speed_mode);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MACID_INFO, (void*)&sgmac_id);
    drv_greatbelt_set_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_CHANNEL_INFO, (void*)&chan_id);
    for (index=0; index<4; index++)
    {
        /* get serdes information */
        CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id+index, DRV_CHIP_SERDES_MODE_INFO,
                                    (void*)&pcs_mode));

        if (pcs_mode == DRV_SERDES_XGSGMII_MODE)
        {
            sys_greatbelt_port_switch_dest_port(lchip, lport_valid, sgmac_id+index, dest_lport+index);
        }
    }
    /*wait mac sync up */
    sal_task_sleep(100);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_to_xfi_mode(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    drv_datapath_serdes_info_t serdes_info;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 field_value = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Serdes infor: hssid=%d, lane_idx=%d, mac_id=%d \n",
        serdes_info.hss_id, serdes_info.lane_idx, serdes_info.mac_id);

    if (serdes_info.mode == DRV_SERDES_XGSGMII_MODE)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_xsgmii_to_xfi(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else if (serdes_info.mode == DRV_SERDES_SGMII_MODE)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_sgmii_to_xfi(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Switch to Xfi failed, the original serdes mode is not SGMII or XSGMII, please check!!\n");
        return CTC_E_INVALID_PARAM;
    }

    /* fix xfi jitter issue */
    drv_greatbelt_chip_write_hss12g(lchip, (serdes_idx/8), ((0x10<<6)|0x0a), 7);
    /*enable dpc*/
    field_value = 0xffff;
    drv_greatbelt_chip_write_hss12g(lchip, (serdes_idx/8), ((rx_lane_addr[serdes_info.lane_idx]<<6)|0x1f), field_value);

    sal_task_sleep(100);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_to_sgmii_mode(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    drv_datapath_serdes_info_t serdes_info;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 field_value = 0;
    uint8 hss_id = serdes_idx/8;
    uint8 index = 0;
    uint8 is_have_xfi = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Serdes infor: hssid=%d, lane_idx=%d, mac_id=%d\n",
        serdes_info.hss_id, serdes_info.lane_idx, serdes_info.mac_id);

    if ((serdes_info.mode == DRV_SERDES_SGMII_MODE) || (serdes_info.mode == DRV_SERDES_XGSGMII_MODE))
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No need to switch! \n");
        return CTC_E_NONE;
    }


    if (serdes_info.mode == DRV_SERDES_XAUI_MODE)
    {
        if (serdes_idx >= 24)
        {
            return CTC_E_INVALID_PARAM;
        }
         /* switch from xaui mode to sgmii mode */
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_xaui_to_sgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else if (serdes_info.mode == DRV_SERDES_XFI_MODE)
    {
        if (serdes_idx >= 24)
        {
            return CTC_E_INVALID_PARAM;
        }
         /* switch from xfi mode to xsgmii mode */
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_xfi_to_sgmii(lchip, serdes_idx, lport_valid, dest_lport));
        /* fix xfi jitter issue */
        for (index = hss_id*8; index < hss_id*8+8; index++)
        {
            drv_greatbelt_get_serdes_info(lchip, index, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info);

            if (DRV_SERDES_XFI_MODE == serdes_info.mode)
            {
               is_have_xfi = 1;
               break;
            }
        }
        if (!is_have_xfi)
        {
            drv_greatbelt_chip_write_hss12g(lchip, (serdes_idx/8), ((0x10<<6)|0x0a), 4);
        }
        /*disable dpc*/
        field_value = 0x7fff;
        drv_greatbelt_chip_write_hss12g(lchip, (serdes_idx/8), ((rx_lane_addr[serdes_info.lane_idx]<<6)|0x1f), field_value);
    }
    else if (serdes_info.mode == DRV_SERDES_2DOT5_MODE)
    {
         /* switch from 2.5g mode to sgmii mode */
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_2dot5_to_sgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else if (serdes_info.mode == DRV_SERDES_QSGMII_MODE)
    {
         /* switch from qsgmii mode to sgmii mode */
         CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_qsgmii_to_sgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else
    {
        /* not support */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes %d mode %d not support switch to sgmii! \n", serdes_idx, serdes_info.mode);
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_to_xsgmii_mode(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    drv_datapath_serdes_info_t serdes_info;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 field_value = 0;
    uint8 hss_id = serdes_idx/8;
    uint8 index = 0;
    uint8 is_have_xfi = 0;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "Serdes infor: hssid=%d, lane_idx=%d, mac_id=%d\n",
        serdes_info.hss_id, serdes_info.lane_idx, serdes_info.mac_id);

    if ((serdes_info.mode == DRV_SERDES_SGMII_MODE) || (serdes_info.mode == DRV_SERDES_XGSGMII_MODE))
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No need to switch! \n");
        return CTC_E_NONE;
    }

    if (serdes_info.mode == DRV_SERDES_XAUI_MODE)
    {
         /* switch from xaui mode to xsgmii mode */
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_xaui_to_xsgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else if (serdes_info.mode == DRV_SERDES_XFI_MODE)
    {
         /* switch from xfi mode to xsgmii mode */
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_xfi_to_xsgmii(lchip, serdes_idx, lport_valid, dest_lport));
        /* fix xfi jitter issue */
        for (index = hss_id*8; index < hss_id*8+8; index++)
        {
            drv_greatbelt_get_serdes_info(lchip, index, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info);

            if (DRV_SERDES_XFI_MODE == serdes_info.mode)
            {
               is_have_xfi = 1;
               break;
            }
        }
        if (!is_have_xfi)
        {
            drv_greatbelt_chip_write_hss12g(lchip, (serdes_idx/8), ((0x10<<6)|0x0a), 4);
        }
        /*disable dpc*/
        field_value = 0x7fff;
        drv_greatbelt_chip_write_hss12g(lchip, (serdes_idx/8), ((rx_lane_addr[serdes_info.lane_idx]<<6)|0x1f), field_value);
    }
    else if (serdes_info.mode == DRV_SERDES_QSGMII_MODE)
    {
         /* switch from qsgmii mode to xsgmii mode */
         CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_qsgmii_to_xsgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else
    {
        /* not support */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes %d mode %d not support switch to xsgmii! \n", serdes_idx, serdes_info.mode);
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_switch_to_xaui_mode(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    drv_datapath_serdes_info_t serdes_info;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    if (serdes_info.mode == DRV_SERDES_XGSGMII_MODE)
    {
        /* do switch from xsgmii to xaui */
       CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_xsgmii_to_xaui(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else if (serdes_info.mode == DRV_SERDES_SGMII_MODE)
    {
        /* do switch from sgmii to xaui */
       CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_sgmii_to_xaui(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else
    {
        /* not support */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes %d mode %d not support switch to xaui! \n", serdes_idx, serdes_info.mode);
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

int32
_sys_greatbelt_chip_switch_to_qsgmii_mode(uint8 lchip, uint8 serdes_idx, uint8 lport_valid, uint16 dest_lport)
{
    drv_datapath_serdes_info_t serdes_info;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));

    /* get serdes information */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO,
                                (void*)&serdes_info));

    if (DRV_SERDES_SGMII_MODE == serdes_info.mode)
    {
        if (serdes_idx >= 24)
        {
            return CTC_E_INVALID_PARAM;
        }

        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_sgmii_to_qsgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else if (DRV_SERDES_XGSGMII_MODE == serdes_info.mode)
    {
        /* the same as sgmii to qsgmii */
        CTC_ERROR_RETURN(_sys_greatbelt_chip_switch_sgmii_to_qsgmii(lchip, serdes_idx, lport_valid, dest_lport));
    }
    else
    {
        /* not support */
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes %d mode %d not support switch to qsgmii! \n", serdes_idx, serdes_info.mode);
        return CTC_E_NOT_SUPPORT;
    }

    return CTC_E_NONE;
}

/* dynamic switch */
int32
sys_greatbelt_chip_set_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info)
{
    uint8 serdes_idx = 0;
    uint8 serdes_idx_temp = 0;
    drv_datapath_serdes_info_t serdes_info;
    ctc_chip_serdes_mode_t serdes_mode;
    uint8 need_clear = 0; /* 1:clear, 0: not clear */
    uint8 old_mac_id = 0; /* mac id before switch, sgmii:0-48, xsgmii:0-11 */
    uint8 new_mac_id = 0; /* mac id after switch,  sgmii:0-48, xsgmii:0-11 */
    uint8 old_mode = CTC_CHIP_SERDES_SGMII_MODE; /* pcs mode before switch */
    int32 ret = CTC_E_NONE;
    uint16 dest_lport = 0;
    uint8 lport_index = 0;
    uint8 lport_num = 0;
    uint8 serdes_index = 0;
    uint8 serdes_num = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;
    drv_datapath_port_capability_t port_cap;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return ret;
    }
    CTC_PTR_VALID_CHECK(p_serdes_info);

    if (HW_PLATFORM == platform_type)
    {
        serdes_mode = p_serdes_info->serdes_mode;
        serdes_idx = p_serdes_info->serdes_id;

        sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip:%d serdes_idx:0x%x serdes_mode:0x%x\n", lchip, serdes_idx, serdes_mode);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip:%d lport_valid:0x%x lport:0x%x\n", lchip, p_serdes_info->lport_valid, p_serdes_info->lport);

        if (serdes_idx >= HSS_SERDES_LANE_NUM)
        {
            ret = CTC_E_EXCEED_MAX_SIZE;
            goto error;
        }

        /* check whether support dynamic switch */
        /* get serdes information */
        CTC_ERROR_GOTO(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO, (void*)&serdes_info), ret, error);
        if (!serdes_info.dynamic)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes not supprt dynamic switch \n" );
            ret = CTC_E_NOT_SUPPORT;
            goto error;
        }

        if (p_serdes_info->lport_valid == 1)
        {
            dest_lport = p_serdes_info->lport;
            if (CTC_CHIP_SERDES_QSGMII_MODE == serdes_mode)
            {
                lport_num = 4;
                serdes_num = 4;
                serdes_idx_temp = serdes_idx;
            }
            else if ((DRV_SERDES_XAUI_MODE  == serdes_info.mode)
                && ((CTC_CHIP_SERDES_SGMII_MODE == serdes_mode) || (CTC_CHIP_SERDES_XSGMII_MODE == serdes_mode)))
            {
                serdes_num = 8;
                lport_num = 8;
                serdes_idx_temp = serdes_idx/8*8;
            }
            else
            {
                lport_num = 1;
                serdes_num = 1;
                serdes_idx_temp = serdes_idx;
            }
            for (lport_index=0; lport_index<lport_num; lport_index++)
            {
                drv_greatbelt_get_port_capability(lchip, dest_lport+lport_index, &port_cap);

                if (port_cap.valid && (0xFF != port_cap.mac_id))
                {
                    for (serdes_index=0; serdes_index<serdes_num; serdes_index++)
                    {
                        if (port_cap.serdes_id == serdes_idx_temp+serdes_index)
                        {
                            break;
                        }
                    }
                    if (serdes_index == serdes_num)
                    {
                        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes not supprt dynamic switch to exist port\n" );
                        ret = CTC_E_NOT_SUPPORT;
                        goto error;
                    }
                }
            }
        }

        if ((DRV_SERDES_SGMII_MODE  == serdes_info.mode) ||
            (DRV_SERDES_XGSGMII_MODE == serdes_info.mode))
        {
            need_clear = 1;
            if (DRV_SERDES_SGMII_MODE  == serdes_info.mode)
            {
                old_mac_id = serdes_info.mac_id;
                old_mode = DRV_SERDES_SGMII_MODE;
            }
            else
            {
                old_mac_id = serdes_info.mac_id - SYS_MAX_GMAC_PORT_NUM;
                old_mode = DRV_SERDES_XGSGMII_MODE;
            }
        }

        switch(serdes_mode)
        {
        case CTC_CHIP_SERDES_XFI_MODE:
            /* switch to xfi mode */
            CTC_ERROR_GOTO(_sys_greatbelt_chip_switch_to_xfi_mode(lchip, serdes_idx, p_serdes_info->lport_valid, p_serdes_info->lport), ret, error);
            break;

        case CTC_CHIP_SERDES_SGMII_MODE:
            /* switch to sgmii mode, used for qsgmii switch sgmii and xaui switch sgmii xfi switch to sgmii */
            CTC_ERROR_GOTO(_sys_greatbelt_chip_switch_to_sgmii_mode(lchip, serdes_idx, p_serdes_info->lport_valid, p_serdes_info->lport), ret, error);
            /* get serdes information */
            sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));
            CTC_ERROR_GOTO(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO, (void*)&serdes_info), ret, error);
            new_mac_id = serdes_info.mac_id;
            CTC_ERROR_GOTO(_sys_greatbelt_chip_set_ignore_failure(lchip, new_mac_id, 1, DRV_SERDES_SGMII_MODE), ret, error);
            break;

        case CTC_CHIP_SERDES_XSGMII_MODE:
            /* switch to xsgmii mode, used for qsgmii switch xsgmii and xfi switch xsgmii */
            CTC_ERROR_GOTO(_sys_greatbelt_chip_switch_to_xsgmii_mode(lchip, serdes_idx, p_serdes_info->lport_valid, p_serdes_info->lport), ret, error);
            /* get serdes information */
            sal_memset(&serdes_info, 0, sizeof(drv_datapath_serdes_info_t));
            CTC_ERROR_GOTO(drv_greatbelt_get_serdes_info(lchip, serdes_idx, DRV_CHIP_SERDES_ALL_INFO, (void*)&serdes_info), ret, error);
            new_mac_id = serdes_info.mac_id - SYS_MAX_GMAC_PORT_NUM;
            CTC_ERROR_GOTO(_sys_greatbelt_chip_set_ignore_failure(lchip, new_mac_id, 1, DRV_SERDES_XGSGMII_MODE), ret, error);
            break;

        case CTC_CHIP_SERDES_QSGMII_MODE:
            /* switch to qsgmii mode, used for sgmii to qsgmii */
            CTC_ERROR_GOTO(_sys_greatbelt_chip_switch_to_qsgmii_mode(lchip, serdes_idx, p_serdes_info->lport_valid, p_serdes_info->lport), ret, error);
            break;

        case CTC_CHIP_SERDES_XAUI_MODE:
            /* switch to xsgmii mode, used for xsgmii to xaui(hss3) and sgmii to xaui(hss0~2)*/
            CTC_ERROR_GOTO(_sys_greatbelt_chip_switch_to_xaui_mode(lchip, serdes_idx, p_serdes_info->lport_valid, p_serdes_info->lport), ret, error);
            break;

        case CTC_CHIP_SERDES_2DOT5G_MODE:
            /* switch to xsgmii mode, used sgmii to 2.5g*/
            CTC_ERROR_GOTO(_sys_greatbelt_chip_switch_to_2dot5_mode(lchip, serdes_idx, p_serdes_info->lport_valid, p_serdes_info->lport), ret, error);
            break;

        default:
            ret = CTC_E_INVALID_PARAM;
            goto error;
        }
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Not support on UML \n");
    }
    /* clear old mac ingnor failure */
    if (need_clear)
    {
        CTC_ERROR_GOTO(_sys_greatbelt_chip_set_ignore_failure(lchip, old_mac_id, 0, old_mode), ret, error);
    }

error:
    /* disable serdes tx */
    if((CTC_CHIP_SERDES_XAUI_MODE != p_serdes_info->serdes_mode) && (CTC_CHIP_SERDES_QSGMII_MODE != p_serdes_info->serdes_mode))
    {
        sys_greatbelt_chip_set_serdes_tx_en(lchip, p_serdes_info->serdes_id, FALSE);
    }

    return ret;
}

/*static bool
_sys_greatbelt_chip_check_ffe_pos(uint8 lchip, uint16 value)
{
    if ((value >> 15) & 0x01)
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}*/

/* ffe_c0: bit15 indacate negative or positive */

STATIC int32
_sys_greatbelt_chip_set_ffe_serdes_eqmode(uint8 lchip, uint8 serdes_id, uint8 enable)
{
    int8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 tx_idx[8] = {0, 1, 4, 5,  8,  9,  12, 13};
    uint16 value = 0;

    hssid = serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x2 , &value);
    if(enable)
    {
        value |= 0x0010; /* used for 802.3ap mode ffe */
    }
    else
    {
        value &= 0xffef; /* used for traditional mode ffe */
    }
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x2, value);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_get_ffe_serdes_eqmode(uint8 lchip, uint8 serdes_id, uint8* p_value)
{
    int8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 tx_idx[8] = {0, 1, 4, 5,  8,  9,  12, 13};
    uint16 value = 0;

    hssid = serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x2 , &value);
    *p_value = CTC_IS_BIT_SET(value, 4);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_ffe_param_define(uint8 lchip, uint8 serdes_id, uint16 ffe_c0, uint16 ffe_c1, uint16 ffe_c2)
{
    uint8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 tx_idx[8] = {0, 1, 4, 5,  8,  9,  12, 13};
    uint16 temp = 0;

    hssid = serdes_id/8;

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lane_idx:%d c0:0x%x c1:0x%x c2;0x%x\n",
        lane_idx, ffe_c0, ffe_c1, ffe_c2);

    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_serdes_eqmode(lchip, serdes_id, 0));
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_FFE_C0_OFFSET, (ffe_c0&0x7fff));
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_FFE_C1_OFFSET, (ffe_c1&0x7fff));
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_FFE_C2_OFFSET, (ffe_c2&0x7fff));

    /* cfg polarity */
    temp = 0x22;
    /*if (_sys_greatbelt_chip_check_ffe_pos(lchip, ffe_c0))
    {
        temp |= 0x01;
    }

    if (_sys_greatbelt_chip_check_ffe_pos(lchip, ffe_c1))
    {
        temp |= 0x02;
    }

    if (_sys_greatbelt_chip_check_ffe_pos(lchip, ffe_c2))
    {
        temp |= 0x04;
    }*/

    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_POLARITY_OFFSET, temp);

    /* apply */
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|2, 0);
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|2, 1);
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|2, 0);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_ffe_param_3ap(uint8 lchip, uint8 serdes_id, uint16 ffe_c0, uint16 ffe_c1, uint16 ffe_c2)
{
    uint8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 tx_idx[8] = {0, 1, 4, 5, 8, 9, 12, 13};
    uint16 value = 0;

    hssid = serdes_id/8;

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lane_idx:%d c0:0x%x c1:0x%x c2;0x%x\n",
        lane_idx, ffe_c0, ffe_c1, ffe_c2);


    /* step 1, CFG AESRC, use pin(3ap train) or register FFE param */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x02, &value);
    value &= 0xffdf;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x02, value);

    CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_serdes_eqmode(lchip, serdes_id, 1));

    /* step 2, cfg Vm Maximum Value Register, max +60 */
    value = 0x3d;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0xf;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

    /* step 3, cfg V2 Limit Extended Register, min 0 */
    value = 0x00;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x11;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

    /* step 4, cfg C0 Limit Extended Register: 0-0xf */
    value = 0x11;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x05;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

    /* step 5, cfg C1 Limit Extended Register 0x0-0x3C */
    value = 0x3d00;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x09;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

    /* step 6, cfg C2 Limit Extended Register 0-0x1F */
    value = 0x0021;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x0d;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

     /*C0 bit[4:0]*/
    value = ffe_c0;
    value = (~value);
    value &= 0x000f;
    value |= 0x0010;
    value += 1;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x03;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

     /*C1 bit[6:0]*/
    value = ffe_c1;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x07;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

     /*C2 bit[5:0]*/
    value = ffe_c2;
    value = (~value);
    value &= 0x001f;
    value |= 0x0020;
    value += 1;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1e, value);

    value = 0x0b;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1f, value);

    /* cfg Transmit 802.3ap Adaptive Equalization Command Register */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x0e, &value);
    value |= 0x1000;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x0e, value);

    /* write 0 to bits[13:0] 0f the command register to put all of the coefficients in the hold state */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x0e, &value);
    value &= 0xc000;
    drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x0e, value);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_check_serdes_ffe(uint8 lchip, ctc_chip_serdes_ffe_t* p_ffe)
{
    uint16 c0 = 0;
    uint16 c1 = 0;
    uint16 c2 = 0;

    c0 = p_ffe->coefficient[0];
    c1 = p_ffe->coefficient[1];
    c2 = p_ffe->coefficient[2];

    if (CTC_CHIP_SERDES_FFE_MODE_TYPICAL == p_ffe->mode)
    {
        return CTC_E_NONE;
    }

    if ((c0+c1+c2) > 60)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0+c1+c2<=60\n", p_ffe->serdes_id);
        return CTC_E_INVALID_PARAM;
    }

    if(CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)
    {
        if ((c0+c2) >= c1)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c1>c0+c2\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }

        if (c0>15)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0<=15\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
        if (c1>60)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c1<=60\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
        if (c2>30)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c2<=30\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        if (c0>25)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c0<=25\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
        if ((c1>60) || (c1 <13))
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must 13<=c1<=60\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
        if (c2>48)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_ERROR,"serdes_id:%d, the ffe param must c2<=48\n", p_ffe->serdes_id);
            return CTC_E_INVALID_PARAM;
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_ffe(uint8 lchip, ctc_chip_serdes_ffe_t* p_ffe)
{
    uint8 serdes_id = 0;

    serdes_id = p_ffe->serdes_id;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "serdes_id:%d board_material:0x%x trace_len:0x%x\n",
        serdes_id, p_ffe->board_material, p_ffe->trace_len);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "c0:0x%x c1:0x%x c2;0x%x\n",
        p_ffe->coefficient[0], p_ffe->coefficient[1], p_ffe->coefficient[2]);

    CTC_ERROR_RETURN(_sys_greatbelt_chip_check_serdes_ffe(lchip, p_ffe));
    /* pff->mode==0 is typical mode, pff->mode==1 is user define mode */
    /* if is typical mode, c0,c1,c2,c3 value is set by motherboard material and trace length */
    if(CTC_CHIP_SERDES_FFE_MODE_TYPICAL == p_ffe->mode)
    {
        if (p_ffe->board_material)
        {
            /* for M4 board */
            if (p_ffe->trace_len == 0)
            {
                /* trace length is 0~4inch c0:0, c1:1a, c2:-d*/
                CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_define(lchip, serdes_id, 0, 0x1a, 0x800d));
            }
            else if (p_ffe->trace_len == 1)
            {
                /* trace length is 4~7inch c0:0, c1:1a, c2:-11*/
                CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_define(lchip, serdes_id, 0, 0x1a, 0x8011));
            }
            else if (p_ffe->trace_len == 2)
            {
                /* trace length is 7~10inch c0:0, c1:1c, c2:-1b*/
                CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_define(lchip, serdes_id, 0, 0x1c, 0x801b));
            }

        }
        else
        {
            /* for TR4 board */
            if (p_ffe->trace_len == 0)
            {
                /* trace length is 0~4inch c0:0, c1:1a, c2;-12*/
                CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_define(lchip, serdes_id, 0, 0x1a, 0x8012));
            }
            else if (p_ffe->trace_len == 1)
            {
                /* trace length is 4~7inch c0:0, c1:1d, c2;-1b*/
                CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_define(lchip, serdes_id, 0, 0x1d, 0x801b));
            }
        }
    }
    else if(CTC_CHIP_SERDES_FFE_MODE_DEFINE == p_ffe->mode)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_define(lchip, serdes_id, p_ffe->coefficient[0], p_ffe->coefficient[1], p_ffe->coefficient[2]));
    }
    else if (CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)
    {
        CTC_ERROR_RETURN(_sys_greatbelt_chip_set_ffe_param_3ap(lchip, serdes_id, p_ffe->coefficient[0], p_ffe->coefficient[1], p_ffe->coefficient[2]));
    }

    return CTC_E_NONE;
}

/* ffe_c0: bit15 indacate negative or positive */
STATIC int32
_sys_greatbelt_chip_get_ffe_param_define(uint8 lchip, uint8 serdes_id, uint16* p_ffe_c0, uint16* p_ffe_c1, uint16* p_ffe_c2)
{
    uint8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 tx_idx[8] = {0, 1, 4, 5, 8, 9, 12, 13};

    hssid = serdes_id/8;

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_FFE_C0_OFFSET, p_ffe_c0);
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_FFE_C1_OFFSET, p_ffe_c1);
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|SYS_CHIP_FFE_C2_OFFSET, p_ffe_c2);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lane_idx:%d c0:0x%x c1:0x%x c2;0x%x\n",
        lane_idx, *p_ffe_c0, *p_ffe_c1, *p_ffe_c2);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_get_ffe_param_3ap(uint8 lchip, uint8 serdes_id, uint16* p_ffe_c0, uint16* p_ffe_c1, uint16* p_ffe_c2)
{
    uint8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 tx_idx[8] = {0, 1, 4, 5, 8, 9, 12, 13};
    uint16 value = 0;

    hssid = serdes_id/8;

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    /* c0 */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x18, &value);
    value &= 0x1f;
    *p_ffe_c0 = (value/2);

    /* c2 */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x1a, &value);
    value &= 0x3f;
    *p_ffe_c2 = (value/2);

    /* c1 */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_idx[lane_idx]<<6)|0x19, &value);
    value &= 0x3f;
    *p_ffe_c1 = ((value-(*p_ffe_c0))-(*p_ffe_c2));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lane_idx:%d c0:0x%x c1:0x%x c2:0x%x\n",
        lane_idx, *p_ffe_c0, *p_ffe_c1, *p_ffe_c2);

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_get_serdes_ffe(uint8 lchip, ctc_chip_serdes_ffe_t* p_ffe)
{
    uint8 serdes_id = 0;
    uint16 ffe_c0 = 0;
    uint16 ffe_c1 = 0;
    uint16 ffe_c2 = 0;
    uint8 enq_mode = 0;

    serdes_id = p_ffe->serdes_id;

    if (serdes_id >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "serdes_id:%d \n", serdes_id);

    if((CTC_CHIP_SERDES_FFE_MODE_TYPICAL == p_ffe->mode) || (CTC_CHIP_SERDES_FFE_MODE_DEFINE == p_ffe->mode))
    {
        _sys_greatbelt_chip_get_ffe_param_define(lchip, serdes_id, &ffe_c0, &ffe_c1, &ffe_c2);
    }
    else if (CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)
    {
        _sys_greatbelt_chip_get_ffe_param_3ap(lchip, serdes_id, &ffe_c0, &ffe_c1, &ffe_c2);
    }
    else
    {
        return CTC_E_NOT_SUPPORT;
    }

    _sys_greatbelt_chip_get_ffe_serdes_eqmode(lchip, serdes_id, &enq_mode);
    p_ffe->coefficient[0] = ffe_c0;
    p_ffe->coefficient[1] = ffe_c1;
    p_ffe->coefficient[2] = ffe_c2;
    if (((CTC_CHIP_SERDES_FFE_MODE_3AP == p_ffe->mode)&& (1 == enq_mode))
        || ((CTC_CHIP_SERDES_FFE_MODE_TYPICAL == p_ffe->mode || CTC_CHIP_SERDES_FFE_MODE_DEFINE == p_ffe->mode)&& (0 == enq_mode)))
    {
        p_ffe->status = 1;
    }
    else
    {
        p_ffe->status = 0;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_get_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info)
{
    uint8 serdes_id = 0;
    uint8 serdes_mode;

    serdes_id = p_serdes_info->serdes_id;

    if (serdes_id >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_PARAM, "serdes_id:%d \n", serdes_id);

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_MODE_INFO, (void*)&serdes_mode));

    if (DRV_SERDES_SGMII_MODE == serdes_mode)
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_SGMII_MODE;
    }
    else if (DRV_SERDES_QSGMII_MODE == serdes_mode)
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_QSGMII_MODE;
    }
    else if (DRV_SERDES_XAUI_MODE == serdes_mode)
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_XAUI_MODE;
    }
    else if (DRV_SERDES_XGSGMII_MODE == serdes_mode)
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_XSGMII_MODE;
    }
    else if (DRV_SERDES_XFI_MODE == serdes_mode)
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_XFI_MODE;
    }
    else if (DRV_SERDES_2DOT5_MODE == serdes_mode)
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_2DOT5G_MODE;
    }
    else
    {
        p_serdes_info->serdes_mode = CTC_CHIP_SERDES_NONE_MODE;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_prbs_rx(uint8 lchip, ctc_chip_serdes_prbs_t* p_prbs)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint8  rec_test_reg = 0x01;
    uint16 rec_test_tmp = 0;
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    hss_id = p_prbs->serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_prbs->serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, &rec_test_tmp));

    rec_test_tmp |= 0x08;
    switch(p_prbs->polynome_type)
    {
        case CTC_CHIP_SERDES_PRBS7_PLUS:
            rec_test_tmp = (rec_test_tmp&0xfff8)+0;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS7_SUB:
            rec_test_tmp = (rec_test_tmp&0xfff8)+1;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS15_PLUS:
            rec_test_tmp = (rec_test_tmp&0xfff8)+2;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS15_SUB:
            rec_test_tmp = (rec_test_tmp&0xfff8)+3;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS23_PLUS:
            rec_test_tmp = (rec_test_tmp&0xfff8)+4;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS23_SUB:
            rec_test_tmp = (rec_test_tmp&0xfff8)+5;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS31_PLUS:
            rec_test_tmp = (rec_test_tmp&0xfff8)+6;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        case CTC_CHIP_SERDES_PRBS31_SUB:
            rec_test_tmp = (rec_test_tmp&0xfff8)+7;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
            break;
        default :
            return CTC_E_INVALID_PARAM;
    }

    /* prbs reset(bit4) */
    rec_test_tmp |= 0x10;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
    sal_task_sleep(1);
    rec_test_tmp &= 0xffef;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));

    /*get prbs rx check result*/
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, &rec_test_tmp));
    p_prbs->value = (uint8)((rec_test_tmp & 0x300)>>8);

    /*disable prbs rx check*/
    rec_test_tmp &= 0xfff7;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));

    /* prbs reset(bit4) */
    rec_test_tmp |= 0x10;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));
    rec_test_tmp &= 0xffef;
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_test_reg, rec_test_tmp));

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_set_serdes_prbs_tx(uint8 lchip, ctc_chip_serdes_prbs_t* p_prbs)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint8  tx_test_reg = 0x01;
    uint16 tx_test_tmp = 0;
    uint32 tx_lane_addr[8] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d};

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    hss_id = p_prbs->serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_prbs->serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, &tx_test_tmp));

    /*value: 1--enable, 0--disbale*/
    if(p_prbs->value)
    {
        tx_test_tmp |= 0x08;
        switch(p_prbs->polynome_type)
        {
            case CTC_CHIP_SERDES_PRBS7_PLUS:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 0;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS7_SUB:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 1;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS15_PLUS:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 2;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS15_SUB:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 3;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS23_PLUS:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 4;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS23_SUB:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 5;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS31_PLUS:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 6;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            case CTC_CHIP_SERDES_PRBS31_SUB:
                tx_test_tmp = (tx_test_tmp & 0xfff8) + 7;
                CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
                break;
            default :
                return CTC_E_INVALID_PARAM;
        }
    }
    else
    {
        tx_test_tmp &= 0xfff7;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_test_reg, tx_test_tmp));
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_prbs(uint8 lchip, ctc_chip_serdes_prbs_t* p_prbs)
{
    uint8 mac_id = 0;
    uint8 gchip = 0;
    uint8 lport = 0;
    uint32 enable = 0;

    CTC_PTR_VALID_CHECK(p_prbs);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, value:%d, type:%d, mode:%d\n", p_prbs->serdes_id, p_prbs->value, p_prbs->mode, p_prbs->polynome_type);

    /* check, if mac disable, can not do prbs */
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_prbs->serdes_id, DRV_CHIP_SERDES_MACID_INFO, (void*)&mac_id));
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO,"mac_id:%d\n", mac_id);
    sys_greatbelt_get_gchip_id(lchip, &gchip);
    lport = SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id);
    sys_greatbelt_port_get_mac_en(lchip, CTC_MAP_LPORT_TO_GPORT(gchip, lport), &enable);
    if (!enable)
    {
        return CTC_E_PORT_MAC_IS_DISABLE;
    }

    switch(p_prbs->mode)
    {
        case 0: /* 0--Rx */
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_prbs_rx(lchip, p_prbs));
            break;
        case 1: /* 1--Tx */
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_prbs_tx(lchip, p_prbs));
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_set_serdes_tx_en(uint8 lchip, uint16 serdes_id, bool enable)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint8  tx_enable_reg = 0x03;
    uint16 value = 0;
    uint32 tx_lane_addr[8] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d};

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    hss_id = serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_enable_reg, &value));
    if(enable)
    {
        value &= 0xffdf;
    }
    else
    {
        value |= 0x0020;
    }
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_enable_reg, value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_serdes_tx_en_with_mac(uint8 lchip, uint8 mac_id)
{
    uint16 lport = 0;
    uint8 serdes_num = 0;
    uint8 index = 0;
    uint16 serdes_id = 0;
    drv_datapath_port_capability_t port_cap;

    lport = sys_greatebelt_common_get_lport_with_mac(lchip, mac_id);
    if (lport == 0xFF)
    {
        return CTC_E_INVALID_PARAM;
    }

    drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
    serdes_id = port_cap.serdes_id;
    if((DRV_SERDES_XAUI_MODE==port_cap.pcs_mode)||(DRV_SERDES_QSGMII_MODE==port_cap.pcs_mode))
    {
        serdes_num = 4;
    }
    else
    {
        serdes_num = 1;
    }

    /* if mac enable, need enable serdes tx */
    for (index = 0; index < serdes_num; index++)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_tx_en(lchip, serdes_id+index, TRUE));
    }

    return CTC_E_NONE;
}


STATIC int32
_sys_greatbelt_chip_set_serdes_loopback_inter(uint8 lchip, ctc_chip_serdes_loopback_t* p_loopback)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint8  loop_back_reg  = 0x01;
    uint8  def_fun1_reg   = 0x1f;
    uint8  rec_sigdet_reg = 0x27;
    uint16 loop_cfg_tmp = 0;
    uint16 def_cfg_tmp = 0;
    uint16 rec_cfg_tmp = 0;
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    hss_id = p_loopback->serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_loopback->serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, &loop_cfg_tmp));
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|def_fun1_reg, &def_cfg_tmp));
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_sigdet_reg, &rec_cfg_tmp));

    if (p_loopback->enable)
    {
        loop_cfg_tmp |= 0x60;
        def_cfg_tmp  &= 0xfdff;
        rec_cfg_tmp  &= 0xfbff;
        rec_cfg_tmp  |= 0x20;
    }
    else
    {
        loop_cfg_tmp &= 0xff9f;
        def_cfg_tmp  |= 0x200;
        rec_cfg_tmp  &= 0xff9f;
    }

    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|loop_back_reg, loop_cfg_tmp));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|def_fun1_reg, def_cfg_tmp));
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rec_sigdet_reg, rec_cfg_tmp));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_loopback_exter(uint8 lchip, ctc_chip_serdes_loopback_t* p_loopback)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint8  speed = 0;
    uint8  mode = 0;
    uint8  tx_mode_reg = 0;
    uint8  rx_mode_reg = 0;
    uint16 tx_cfg_tmp  = 0;
    uint16 rx_cfg_tmp  = 0;
    uint32 tx_lane_addr[8] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d};
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint8 auxsel_used = 0;
    uint32 cmd = 0;
    hss12_g0_cfg_t g0_cfg;
    hss12_g1_cfg_t g1_cfg;
    hss12_g2_cfg_t g2_cfg;
    hss12_g3_cfg_t g3_cfg;
    sal_memset(&g0_cfg, 0, sizeof(hss12_g0_cfg_t));
    sal_memset(&g1_cfg, 0, sizeof(hss12_g1_cfg_t));
    sal_memset(&g2_cfg, 0, sizeof(hss12_g2_cfg_t));
    sal_memset(&g3_cfg, 0, sizeof(hss12_g3_cfg_t));

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);

    hss_id = p_loopback->serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_loopback->serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_loopback->serdes_id, DRV_CHIP_SERDES_SPEED_INFO, &speed));
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_loopback->serdes_id, DRV_CHIP_SERDES_SPEED_INFO, &mode));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_mode_reg, &tx_cfg_tmp));
    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rx_mode_reg, &rx_cfg_tmp));

    switch(hss_id)
    {
        case 0:
            cmd = DRV_IOR(Hss12G0Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g0_cfg));
            auxsel_used = g0_cfg.cfg0_h_s_s_e_x_t_c16_s_e_l;
            break;
        case 1:
            cmd = DRV_IOR(Hss12G1Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g1_cfg));
            auxsel_used = g1_cfg.cfg1_h_s_s_e_x_t_c16_s_e_l;
            break;
        case 2:
            cmd = DRV_IOR(Hss12G2Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g2_cfg));
            auxsel_used = g2_cfg.cfg2_h_s_s_e_x_t_c16_s_e_l;
            break;
        case 3:
            cmd = DRV_IOR(Hss12G3Cfg_t, DRV_ENTRY_FLAG);
            CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &g3_cfg));
            auxsel_used = g3_cfg.cfg3_h_s_s_e_x_t_c16_s_e_l;
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }
    if(p_loopback->enable)
    {
        tx_cfg_tmp |= 0x08;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_mode_reg, tx_cfg_tmp));
        if(speed != DRV_SERDES_SPPED_10DOT3125G)
        {
            rx_cfg_tmp |= 0x0c;
            if (auxsel_used && (speed == DRV_SERDES_SPPED_1DOT25G))
            {
                rx_cfg_tmp &= 0xFFFC;
            }
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rx_mode_reg, rx_cfg_tmp));
        }
    }
    else
    {
        tx_cfg_tmp &= 0xfff7;
        CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|tx_mode_reg, tx_cfg_tmp));
        if(speed != DRV_SERDES_SPPED_10DOT3125G && ((mode == DRV_SERDES_SGMII_MODE) ||
            (mode == DRV_SERDES_QSGMII_MODE) || (mode == DRV_SERDES_XAUI_MODE) || (mode == DRV_SERDES_XGSGMII_MODE)))
        {
            rx_cfg_tmp &= 0xfff7;
            rx_cfg_tmp |= 0x04;
            if (auxsel_used && (speed == DRV_SERDES_SPPED_1DOT25G))
            {
                rx_cfg_tmp |= 0x03;
            }
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rx_mode_reg, rx_cfg_tmp));
        }

        if(speed != DRV_SERDES_SPPED_10DOT3125G && ((mode == DRV_SERDES_XFI_MODE) || (mode == DRV_SERDES_INTLK_MODE)))
        {
            rx_cfg_tmp |= 0x0c;
            CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|rx_mode_reg, rx_cfg_tmp));
        }
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_loopback(uint8 lchip, ctc_chip_serdes_loopback_t* p_loopback)
{
    CTC_PTR_VALID_CHECK(p_loopback);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, enable_value:%d, type:%d\n", p_loopback->serdes_id, p_loopback->enable, p_loopback->mode);

    switch(p_loopback->mode)
    {
        case 0: /* 0--interal */
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_loopback_inter(lchip, p_loopback));
            break;
        case 1: /* 1--external */
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_loopback_exter(lchip, p_loopback));
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }
    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_serdes_cfg(uint8 lchip, ctc_chip_property_t chip_prop, ctc_chip_serdes_cfg_t* p_serdes_cfg)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint16 cfg_tmp  = 0;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};
    uint32 addr = 0;

    CTC_PTR_VALID_CHECK(p_serdes_cfg);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d, value:%d\n", p_serdes_cfg->serdes_id, p_serdes_cfg->value);

    if (p_serdes_cfg->serdes_id >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }


    hss_id = p_serdes_cfg->serdes_id/8;


    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_serdes_cfg->serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    switch(chip_prop)
    {
        case CTC_CHIP_PROP_SERDES_PEAK:
            if (p_serdes_cfg->value > 0x7)
            {
                return CTC_E_INVALID_PARAM;
            }
            addr = (rx_lane_addr[lane_idx] << 6)|0x0b;
            CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, addr, &cfg_tmp));
            cfg_tmp &= 0xF8FF;
            cfg_tmp |= (p_serdes_cfg->value <<8);
            break;
        case CTC_CHIP_PROP_SERDES_DPC:
            addr = (rx_lane_addr[lane_idx] << 6)|0x1f;
            CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, addr, &cfg_tmp));
            if(p_serdes_cfg->value)
            {
                cfg_tmp |= (1<<15);
            }
            else
            {
                cfg_tmp &= ~(1<<15);
            }
            break;
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            if (p_serdes_cfg->value >= 3)
            {
                return CTC_E_INVALID_PARAM;
            }
            addr = (tx_lane_addr[lane_idx] << 6)|0x3;
            CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, addr, &cfg_tmp));
            cfg_tmp &= ~(3<<2);
            cfg_tmp |= (p_serdes_cfg->value<<2);
            break;
        default:
            return CTC_E_INVALID_PARAM;
            break;
    }

    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hss_id, addr, cfg_tmp));

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_get_serdes_cfg(uint8 lchip, ctc_chip_property_t chip_prop, ctc_chip_serdes_cfg_t* p_serdes_cfg)
{
    uint8  hss_id = 0;
    uint8  lane_idx = 0;
    uint16 cfg_tmp  = 0;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};

    CTC_PTR_VALID_CHECK(p_serdes_cfg);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_PARAM,"serdes_id:%d\n", p_serdes_cfg->serdes_id);

    if (p_serdes_cfg->serdes_id >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    hss_id = p_serdes_cfg->serdes_id/8;
    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, p_serdes_cfg->serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    switch(chip_prop)
    {
        case CTC_CHIP_PROP_SERDES_PEAK:
            CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0x0b, &cfg_tmp));
            p_serdes_cfg->value = ((cfg_tmp>>8)&0x7);
            break;
        case CTC_CHIP_PROP_SERDES_DPC:
            CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (rx_lane_addr[lane_idx] << 6)|0x1f, &cfg_tmp));
            if (CTC_IS_BIT_SET(cfg_tmp, 15))
            {
                p_serdes_cfg->value = TRUE;
            }
            else
            {
                p_serdes_cfg->value = FALSE;
            }
            break;
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hss_id, (tx_lane_addr[lane_idx] << 6)|0x3, &cfg_tmp));
            p_serdes_cfg->value = (cfg_tmp>>2)&0x3;
            break;
        default:
            return CTC_E_INVALID_PARAM;
            break;
    }

    return CTC_E_NONE;
}

STATIC int32
_sys_greatbelt_chip_set_global_eee_en(uint8 lchip, uint32* enable)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    field_value = ((*enable) ? 1 : 0);
    cmd = DRV_IOW(NetTxMiscCtl_t, NetTxMiscCtl_EeeCtlEnable_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

int32
_sys_greatbelt_chip_set_mem_bist(uint8 lchip, ctc_chip_mem_bist_t* p_value)
{
    uint32 index = 0;
    uint32 cmd = 0;
    uint32 value = 0;
    uint32 dynamic_ds_edram4w_num = 0;
    dynamic_ds_edram4_w_t dynamic_ds_edram4w;

    CTC_PTR_VALID_CHECK(p_value);

    if (1 == SDK_WORK_PLATFORM)
    {
        return CTC_E_NONE;
    }

    CTC_ERROR_RETURN(sys_greatbelt_ftm_get_table_entry_num(lchip, DynamicDsEdram4W_t, &dynamic_ds_edram4w_num));

    sal_memset(&dynamic_ds_edram4w, 0, sizeof(dynamic_ds_edram4_w_t));

    for (index = 0; index < dynamic_ds_edram4w_num; index++)
    {
        if ((index % 1000) == 0)
        {
            sal_task_sleep(2);
        }
        cmd = DRV_IOR(DynamicDsEdram4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &dynamic_ds_edram4w));
        dynamic_ds_edram4w.ram_data31_to0 = 0xFFFFFFFF;
        dynamic_ds_edram4w.ram_data63_to32 = 0xFFFFFFFF;
        dynamic_ds_edram4w.ram_data78_to64 = 0x7FFF;
        cmd = DRV_IOW(DynamicDsEdram4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &dynamic_ds_edram4w));

        value = (index >= DYNAMIC_EDRAM_STATS_INDEX_BASE) ? 0xFF : 0x7FFF;

        cmd = DRV_IOR(DynamicDsEdram4W_t, DRV_ENTRY_FLAG);
        CTC_ERROR_RETURN(DRV_IOCTL(lchip, index, cmd, &dynamic_ds_edram4w));
        if (dynamic_ds_edram4w.ram_data31_to0 != 0xFFFFFFFF ||
            dynamic_ds_edram4w.ram_data63_to32 != 0xFFFFFFFF ||
            dynamic_ds_edram4w.ram_data78_to64 != value)
        {
            p_value->status = 1;
            return CTC_E_NONE;
        }

    }

    p_value->status = 0;
    return CTC_E_NONE;

}


int32
sys_greatbelt_chip_set_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value)
{
    uint32 enable = 0;
    ctc_chip_gpio_params_t* p_gpio_param = NULL;
    ctc_chip_phy_mapping_para_t* p_phy_mapping = NULL;

    CTC_PTR_VALID_CHECK(p_value);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip:%d chip_prop:0x%x \n", lchip, chip_prop);

    switch(chip_prop)
    {
        case CTC_CHIP_PROP_SERDES_FFE:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_ffe(lchip, (ctc_chip_serdes_ffe_t*)p_value));
            break;

        case CTC_CHIP_PROP_SERDES_PRBS:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_prbs(lchip, (ctc_chip_serdes_prbs_t*)p_value));
            break;

        case CTC_CHIP_PROP_SERDES_LOOPBACK:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_loopback(lchip,(ctc_chip_serdes_loopback_t*)p_value));
            break;

        case CTC_CHIP_PROP_SERDES_PEAK:
        case CTC_CHIP_PROP_SERDES_DPC:
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_serdes_cfg(lchip, chip_prop, (ctc_chip_serdes_cfg_t*)p_value));
            break;

        case CTC_CHIP_PROP_EEE_EN:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_global_eee_en(lchip, (uint32*)p_value));
            break;

        case CTC_CHIP_PHY_SCAN_EN:
            enable = *((uint32*)p_value);
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_phy_scan_en(lchip, enable));
            break;

        case CTC_CHIP_I2C_SCAN_EN:
            enable = *((uint32*)p_value);
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_i2c_scan_en(lchip, enable));
            break;

        case CTC_CHIP_MAC_LED_EN:
            enable = *((uint32*)p_value);
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_mac_led_en(lchip, enable));
            break;
        case CTC_CHIP_PROP_GPIO_MODE:
            p_gpio_param = (ctc_chip_gpio_params_t*)p_value;
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_gpio_mode(lchip, p_gpio_param->gpio_id, p_gpio_param->value));
            break;
        case CTC_CHIP_PROP_GPIO_OUT:
            p_gpio_param = (ctc_chip_gpio_params_t*)p_value;
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_gpio_output(lchip, p_gpio_param->gpio_id, p_gpio_param->value));
            break;
        case CTC_CHIP_PROP_PHY_MAPPING:
            p_phy_mapping= (ctc_chip_phy_mapping_para_t*)p_value;
            CTC_ERROR_RETURN(sys_greatbelt_chip_set_phy_mapping(lchip, p_phy_mapping));
            break;
        case CTC_CHIP_PROP_MEM_BIST:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_set_mem_bist(lchip, (ctc_chip_mem_bist_t*)p_value));
            break;
        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_get_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value)
{
    ctc_chip_gpio_params_t* p_gpio_param = NULL;
    ctc_port_serdes_info_t* p_serdes_port = NULL;
    ctc_chip_serdes_info_t serdes_info;
    uint8 gchip = 0;
    uint16 lport = 0;
    CTC_PTR_VALID_CHECK(p_value);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n", __FUNCTION__);
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_INFO, "lchip:%d chip_prop:0x%x \n", lchip, chip_prop);


    switch(chip_prop)
    {
        case CTC_CHIP_PROP_DEVICE_INFO:
            CTC_ERROR_RETURN(sys_greatbelt_chip_get_device_info(lchip, (ctc_chip_device_info_t*)p_value));
            break;

        case CTC_CHIP_PROP_GPIO_IN:
            p_gpio_param = (ctc_chip_gpio_params_t*)p_value;
            CTC_ERROR_RETURN(sys_greatbelt_chip_get_gpio_intput(lchip, p_gpio_param->gpio_id, &(p_gpio_param->value)));
            break;

        case CTC_CHIP_PROP_SERDES_ID_TO_GPORT:
            p_serdes_port = (ctc_port_serdes_info_t*)p_value;

            CTC_ERROR_RETURN(drv_greatbelt_get_port_with_serdes(lchip, p_serdes_port->serdes_id, &lport));
            CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, &gchip));
            p_serdes_port->gport= CTC_MAP_LPORT_TO_GPORT(gchip, lport);
            sal_memset(&serdes_info, 0, sizeof(serdes_info));
            serdes_info.serdes_id = p_serdes_port->serdes_id;
            CTC_ERROR_RETURN(sys_greatbelt_chip_get_serdes_mode(lchip, &serdes_info));
            p_serdes_port->serdes_mode = serdes_info.serdes_mode;
            p_serdes_port->overclocking_speed = 0;
            break;
        case CTC_CHIP_PROP_SERDES_FFE:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_get_serdes_ffe(lchip, (ctc_chip_serdes_ffe_t*)p_value));
            break;

        case CTC_CHIP_PROP_SERDES_PEAK:
        case CTC_CHIP_PROP_SERDES_DPC:
        case CTC_CHIP_PROP_SERDES_SLEW_RATE:
            CTC_ERROR_RETURN(_sys_greatbelt_chip_get_serdes_cfg(lchip, chip_prop, (ctc_chip_serdes_cfg_t*)p_value));
            break;

        default:
            return CTC_E_INVALID_PARAM;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_reset_dfe(uint8 lchip, uint8 serdes_id, uint8 enable)
{
    uint8 hssid = 0;
    uint8 lane_idx = 0;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint16 value;
    hssid = serdes_id/8;

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));

    CTC_ERROR_RETURN(drv_greatbelt_chip_read_hss12g(lchip, hssid, (rx_lane_addr[lane_idx] << 6)|0x8, &value));
    if (enable)
    {
        /*reset rx dfe reset*/
        value |= (1 << 0);
    }
    else
    {
        /*release rx dfe reset*/
        value &= ~(1 << 0);
    }
    CTC_ERROR_RETURN(drv_greatbelt_chip_write_hss12g(lchip, hssid, (rx_lane_addr[lane_idx] << 6)|0x8, value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_reset_rx_serdes(uint8 lchip, uint8 serdes_id, uint8 reset)
{
    uint8 hssid = 0;
    uint8 lane_idx = 0;
    uint8 pll_reset[]={2, 3, 6, 7, 2, 3, 6, 7};
    uint16 value;
    hssid = serdes_id/8;

    CTC_ERROR_RETURN(drv_greatbelt_get_serdes_info(lchip, serdes_id, DRV_CHIP_SERDES_LANE_INFO, &lane_idx));
    if(lane_idx < 4)
    {
        drv_greatbelt_chip_read_hss12g(lchip, hssid, (0x10<<6)|SYS_CHIP_PLL_RESET0_OFFSET, &value);
        if(reset)
        {
            value |= (1<<pll_reset[lane_idx]);
        }
        else
        {
            value &= ~(1<<pll_reset[lane_idx]);
        }
        drv_greatbelt_chip_write_hss12g(lchip, hssid, (0x10<<6)|SYS_CHIP_PLL_RESET0_OFFSET, value);
    }
    else
    {
        drv_greatbelt_chip_read_hss12g(lchip, hssid, (0x10<<6)|SYS_CHIP_PLL_RESET1_OFFSET, &value);
        if(reset)
        {
            value |= (1<<pll_reset[lane_idx]);
        }
        else
        {
            value &= ~(1<<pll_reset[lane_idx]);
        }
        drv_greatbelt_chip_write_hss12g(lchip, hssid, (0x10<<6)|SYS_CHIP_PLL_RESET1_OFFSET, value);
    }
    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_reset_rx_hss(uint8 lchip, uint8 hss_id, uint8 reset)
{
    uint16 value;

    drv_greatbelt_chip_read_hss12g(lchip, hss_id, (0x10<<6)|SYS_CHIP_PLL_RESET0_OFFSET, &value);
    if(reset)
    {
        value |= 0xcc;
    }
    else
    {
        value &= ~((uint8)0xcc);
    }
    drv_greatbelt_chip_write_hss12g(lchip, hss_id, (0x10<<6)|SYS_CHIP_PLL_RESET0_OFFSET, value);

    drv_greatbelt_chip_read_hss12g(lchip, hss_id, (0x10<<6)|SYS_CHIP_PLL_RESET1_OFFSET, &value);
    if(reset)
    {
        value |= 0xcc;
    }
    else
    {
        value &= ~((uint8)0xcc);
    }
    drv_greatbelt_chip_write_hss12g(lchip, hss_id, (0x10<<6)|SYS_CHIP_PLL_RESET1_OFFSET, value);

    return CTC_E_NONE;
}

#define __DEBUG_BEGIN__

STATIC void
_sys_chip_show_pll_cfg(drv_datapath_master_t* p_datapath)
{
    uint8 index = 0;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        if (p_datapath->pll_cfg.core_pll.is_used)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Core PLL Configuration: \n");
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll   refclk : %d \n", p_datapath->pll_cfg.core_pll.ref_clk);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll output_a : %d \n", p_datapath->pll_cfg.core_pll.output_a);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll output_b : %d \n", p_datapath->pll_cfg.core_pll.output_b);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll     Cfg1 : 0x%08x\n", p_datapath->pll_cfg.core_pll.core_pll_cfg1);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll     Cfg2 : 0x%08x\n", p_datapath->pll_cfg.core_pll.core_pll_cfg2);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll     Cfg3 : 0x%08x\n", p_datapath->pll_cfg.core_pll.core_pll_cfg3);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll     Cfg4 : 0x%08x\n", p_datapath->pll_cfg.core_pll.core_pll_cfg4);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "CorePll     Cfg5 : 0x%08x\n", p_datapath->pll_cfg.core_pll.core_pll_cfg5);
        }

        if (p_datapath->pll_cfg.tank_pll.is_used)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Tank PLL Configuration: \n");
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll   refclk : %d\n", p_datapath->pll_cfg.tank_pll.ref_clk);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll output_a : %d\n", p_datapath->pll_cfg.tank_pll.output_a);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll output_b : %d\n", p_datapath->pll_cfg.tank_pll.output_b);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll     Cfg1 : 0x%08x\n", p_datapath->pll_cfg.tank_pll.tank_pll_cfg1);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "TankPll     Cfg2 : 0x%08x\n", p_datapath->pll_cfg.tank_pll.tank_pll_cfg2);
        }

        for (index = 0; index < HSS_MACRO_NUM; index++)
        {
            if (p_datapath->pll_cfg.hss_pll[index].is_used)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d PLL Configuration: \n", index);
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%dPll       Cfg1 : 0x%08x\n", index,  p_datapath->pll_cfg.hss_pll[index].hss_pll_cfg1);
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%dPll       Cfg2 : 0x%08x\n", index, p_datapath->pll_cfg.hss_pll[index].hss_pll_cfg2);

                if (p_datapath->pll_cfg.hss_pll[index].aux_sel)
                {
                    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Use Auxsel Clock! \n", index);
                }
                else
                {
                    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Donot Use Auxsel Clock! \n", index);
                }
            }
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }

    return;
}

STATIC void
_sys_chip_show_hss_cfg(drv_datapath_master_t* p_datapath)
{
    uint8 index = 0;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss Configuration: \n");
        for (index = 0; index < HSS_MACRO_NUM; index++)
        {
            if (p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_SGMII_MODE)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Mode: SGMII Mode \n", index);
            }
            else if (p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_QSGMII_MODE)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Mode: QSGMII Mode \n", index);
            }
            else if (p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_XFI_MODE)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Mode: XFI Mode \n", index);
            }
            else if (p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_XAUI_MODE)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Mode: XAUI Mode \n", index);
            }
            else if (p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_INTLK_MODE)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Hss%d Mode: IntrLake Mode \n", index);
            }
            else if ((p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_MIX_MODE_WITH_QSGMII) ||
                     (p_datapath->hss_cfg.hss_mode[index] == DRV_SERDES_MIX_MODE_WITH_XFI) )
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "mix mode or support dynamic switch \n");
            }
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac10 used hss index:0x%x \n", p_datapath->hss_cfg.hss_used_sgmac10);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac11 used hss index:0x%x \n", p_datapath->hss_cfg.hss_used_sgmac11);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac6 used hss index:0x%x \n", p_datapath->hss_cfg.hss_used_sgmac6);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac7 used hss index:0x%x \n", p_datapath->hss_cfg.hss_used_sgmac7);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac8 used hss index:0x%x \n", p_datapath->hss_cfg.hss_used_sgmac8);
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac9 used hss index:0x%x \n", p_datapath->hss_cfg.hss_used_sgmac9);

        if (p_datapath->hss_cfg.sgmac4_is_xaui)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac4 used Xaui Mode \n");
        }
        else
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac4 is not Xaui Mode \n");
        }

        if (p_datapath->hss_cfg.sgmac5_is_xaui)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac5 used Xaui Mode \n");
        }
        else
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Smac4 is not Xaui Mode \n");
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii9 select hss3 : %s \n", (p_datapath->hss_cfg.qsgmii9_sel_hss3 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii8 select hss3 : %s \n", (p_datapath->hss_cfg.qsgmii8_sel_hss3 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii7 select hss2 : %s \n", (p_datapath->hss_cfg.qsgmii7_sel_hss2 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii6 select hss2 : %s \n", (p_datapath->hss_cfg.qsgmii6_sel_hss2 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii3 select hss3 : %s \n", (p_datapath->hss_cfg.qsgmii3_sel_hss3 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii2 select hss3 : %s \n", (p_datapath->hss_cfg.qsgmii2_sel_hss3 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii1 select hss2 : %s \n", (p_datapath->hss_cfg.qsgmii1_sel_hss2 ? ("TRUE"):("FALSE")));
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Qsgmii0 select hss2 : %s \n", (p_datapath->hss_cfg.qsgmii0_sel_hss2 ? ("TRUE"):("FALSE")));

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }
    return;
}

STATIC void
_sys_chip_show_serdes_cfg(drv_datapath_master_t* p_datapath)
{
    uint8 index = 0;
    uint8 value_idx = 0;
    char* mode_str[DRV_HSS_MAX_LANE_MODE] = {"Sgmii","Qsgmii", "Xaui","Sgmii(Sgmac)", "Xfi", "intlk", "NA","NA","NA","NA-SGMII","2.5G"};
    char* speed_str[5] = {"1.25G", "3.125G", "5G", "6.25", "10.3125G"};
    char* str = NULL;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes Configuration: \n");
        for (index = 0; index < HSS_SERDES_LANE_NUM; index++)
        {
            value_idx = p_datapath->serdes_cfg[index].mode;
            str = (char*)mode_str[value_idx];

            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d mode is:%s \n", index, str);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d mac_id is %d \n", index, p_datapath->serdes_cfg[index].mac_id);
            str = (char*)speed_str[p_datapath->serdes_cfg[index].speed];
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d speed is %s \n", index, str);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d chan_id is %d \n", index, p_datapath->serdes_cfg[index].chan_id);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d hssid is %d \n", index, p_datapath->serdes_cfg[index].hss_id);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d serdes lane is %d \n", index, p_datapath->serdes_cfg[index].lane_idx);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "serdes%d dynamic mode is %d \n", index, p_datapath->serdes_cfg[index].dynamic);

            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
        }
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }
    return;
}


STATIC void
_sys_chip_show_mac_cfg(drv_datapath_master_t* p_datapath)
{
    uint8 index = 0;
    uint8 value_idx = 0;
    char* mode_str[7] = {"Sgmii","Qsgmii", "Xaui","Sgmii(Sgmac)", "Xfi", "intlk", "Not use"};
    char* str = NULL;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "QuadMac Configuration: \n");
        for (index = 0; index < QUADMAC_NUM; index++)
        {
            if (p_datapath->quadmac_cfg[index].valid == 0)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "QuadMac%d is not used! \n", index);
            }
            else
            {
                value_idx = p_datapath->quadmac_cfg[index].pcs_mode;
                str = (char*)mode_str[value_idx];
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "QuadMac%d Pcs Mode:%s \n", index, str);
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "QuadMac%d serdes Id:0x%x \n", index, p_datapath->quadmac_cfg[index].serdes_id);
            }
        }
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "GMac Configuration: \n");
        for (index = 0; index < GMAC_NUM; index++)
        {
            if (p_datapath->gmac_cfg[index].valid)
            {
                value_idx = p_datapath->gmac_cfg[index].pcs_mode;

                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "GMac%d pcs_mode:%s, serdes_id:%d,  chan_id:%d\n", index, mode_str[value_idx],
                                 p_datapath->gmac_cfg[index].serdes_id, p_datapath->gmac_cfg[index].chan_id);
            }
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SGMac Configuration: \n");
        for (index = 0; index < SGMAC_NUM; index++)
        {
            if (p_datapath->sgmac_cfg[index].valid == 0)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SGMac%d is not used! \n", index);
            }
            else
            {
                value_idx = p_datapath->sgmac_cfg[index].pcs_mode;

                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SGMac%d Pcs Mode:%s \n", index, mode_str[value_idx]);

                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SGMac%d channel id:0x%x, serdes id:0x%x \n", index, p_datapath->sgmac_cfg[index].chan_id,
                                 p_datapath->sgmac_cfg[index].serdes_id);
            }
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }
    return;
}

STATIC void
_sys_chip_show_syncE_cfg(drv_datapath_master_t* p_datapath)
{
    uint32 index = 0;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SyncE Configuration: \n");

        for (index = 0; index < DRV_SYNCE_ITEM_MAX; index++)
        {
            if (p_datapath->sync_cfg[index].is_used)
            {
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SyncE%d Cfg1: 0x%x \n", index, p_datapath->sync_cfg[index].sync_cfg1);
                SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "SyncE%d Cfg2: 0x%x \n", index, p_datapath->sync_cfg[index].sync_cfg2);
            }
        }

        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }
    return;
}

STATIC void
_sys_chip_show_caled_cfg(drv_datapath_master_t* p_datapath)
{
    uint8 index =0;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Calendar Configuration: \n");
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Calendar sel:%s \n", p_datapath->calendar_sel?"calendar_back":"calendar_ctl");
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Calendar ctl cfg: \n");
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ctl calendar walk end:%x \n", p_datapath->cal_infor[0].cal_walk_end);
        for (index = 0; index < 160; index++)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "index:%d   value:0x%x \n", index, p_datapath->cal_infor[0].net_tx_cal_entry[index]);
        }
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "back calendar walk end:%x \n", p_datapath->cal_infor[1].cal_walk_end);
        for (index = 0; index < 160; index++)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "index:%d   value:0x%x \n", index, p_datapath->cal_infor[1].net_tx_cal_entry[index]);
        }
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }
    return;
}

STATIC void
_sys_chip_show_misc_cfg(drv_datapath_master_t* p_datapath)
{
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }

    if (HW_PLATFORM == platform_type)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Misc Configuration: \n");
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Cpumac type: %s \n",  p_datapath->misc_info.cpumac_type?"NA":"GE");
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Ptp use: %s \n",  p_datapath->misc_info.ptp_used?"Used":"Not Use");
    }
    else
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "No datapath info on UML \n");
    }
    return;
}

STATIC void
_sys_chip_show_chan_cfg(drv_datapath_master_t* p_datapath)
{
    uint8 lport = 0;
    char* port_type_str[DRV_PORT_TYPE_MAX] = {"NONE", "1G", "10G","DMA", "CPU"};
    char* str = NULL;
    int32 ret = 0;
    drv_work_platform_type_t platform_type = HW_PLATFORM;

    ret = drv_greatbelt_get_platform_type(&platform_type);
    if (ret < 0)
    {
        return;
    }
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "channel Configuration: \n");
    for (lport = 0; lport < SYS_GB_MAX_PHY_PORT; lport++)
    {
        if ((platform_type != HW_PLATFORM) && (lport > 56))
        {
            break;
        }
        if (p_datapath->port_capability[lport].valid)
        {
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "local_phy_port: %2d, valid: %d ", lport, p_datapath->port_capability[lport].valid);
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "mac_id: %2d, channel: %2d, ", p_datapath->port_capability[lport].mac_id, p_datapath->port_capability[lport].chan_id);

            str = (char*)port_type_str[p_datapath->port_capability[lport].port_type];
            SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "port_type: %s \n", str);
        }
    }

    /* internal channel info */
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "\n\ninternal channel Configuration: \n");
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "interlaken: %d\n", \
    p_datapath->port_capability[SYS_RESERVE_PORT_ID_INTLK].chan_id);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "oam:        %d\n",  \
    p_datapath->port_capability[SYS_RESERVE_PORT_ID_OAM].chan_id);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "dma:        %d\n",  \
    p_datapath->port_capability[SYS_RESERVE_PORT_ID_DMA].chan_id);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "cpu:        %d\n",  \
    p_datapath->port_capability[SYS_RESERVE_PORT_ID_CPU].chan_id);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "iloop:      %d\n",  \
    p_datapath->port_capability[SYS_RESERVE_PORT_ID_ILOOP].chan_id);

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "eloop:      %d\n",  \
    p_datapath->port_capability[SYS_RESERVE_PORT_ID_ELOOP].chan_id);


    return;
}

STATIC void
_sys_chip_show_rsv_internal_port(void)
{
    uint8 inter_port_value = 0;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Reserved internal lport: \n");
    inter_port_value = SYS_RESERVE_PORT_ID_DROP;

    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "drop       : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_ID_ILOOP;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "iloop      : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_MIRROR;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "mirror     : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_ID_CPU;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "cpu        : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_ID_DMA;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "dma        : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_ID_OAM;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "oam        : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_IP_TUNNEL;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "ip tunnel  : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_MPLS_BFD;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "mpls bfd   : %d\n", inter_port_value);

    inter_port_value = SYS_RESERVE_PORT_PW_VCCV_BFD;
    SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "pw vccv    : %d\n", inter_port_value);

    return;
}
int32
sys_greatbelt_chip_show_datapath (uint8 lchip, sys_datapath_debug_type_t type)
{
    drv_datapath_master_t* p_datapath = NULL;
    SYS_LCHIP_CHECK_ACTIVE(lchip);
    CTC_ERROR_RETURN(drv_greatbelt_get_datapath(lchip, &p_datapath));

    switch (type)
    {
        case SYS_CHIP_DATAPATH_DEBUG_PLL:
            _sys_chip_show_pll_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_HSS:
            _sys_chip_show_hss_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_SERDES:
            _sys_chip_show_serdes_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_MAC:
            _sys_chip_show_mac_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_SYNCE:
            _sys_chip_show_syncE_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_CALDE:
            _sys_chip_show_caled_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_MISC:
            _sys_chip_show_misc_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_CHAN:
            _sys_chip_show_chan_cfg(p_datapath);
            break;
        case SYS_CHIP_DATAPATH_DEBUG_INTER_PORT:
            _sys_chip_show_rsv_internal_port();
            break;

        default:
            break;
    }

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_set_force_signal_detect(uint8 lchip, bool enable)
{
    int32 ret = CTC_E_NONE;

    ret = drv_greatbelt_datapath_force_signal_detect(lchip, enable);
    if (ret < 0)
    {
        SYS_CHIP_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, "Set force signal detect fail!\n");
        ret = CTC_E_INVALID_PARAM;
    }

    return ret;
}

int32
sys_greatbelt_chip_set_dram_wdrr_weight(uint8 lchip, uint8 ram_id, uint8 if_id, uint8 weight)
{

    uint32 cmd = 0;
    uint32 field_value = 1;
    uint32 field_id = 0;
    uint8 step = 0;

    CTC_MAX_VALUE_CHECK(ram_id, 5);
    CTC_MAX_VALUE_CHECK(if_id, 7);

    field_value = weight;

    step = (DynamicDsWrrCreditCfg_Edram1Intf0CreditCfg_f - DynamicDsWrrCreditCfg_Edram0Intf0CreditCfg_f);
    field_id = ram_id * step + if_id;
    cmd = DRV_IOW(DynamicDsWrrCreditCfg_t, field_id);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return CTC_E_NONE;
}

int32
sys_greatbelt_chip_reset_mdio(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    uint32 cmd = 0;
    uint32 field_value = 1;
    cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetMdio_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    sal_task_sleep(1);

    field_value = 0;
    cmd = DRV_IOW(ResetIntRelated_t, ResetIntRelated_ResetMdio_f);
    CTC_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    return ret;
}

int32
sys_greatbelt_chip_device_check(uint8 lchip)
{
    int32 ret = CTC_E_NONE;
    ctc_chip_device_info_t device_info;
    drv_work_platform_type_t platform_type;

    /* check device feature */
    CTC_ERROR_RETURN(drv_greatbelt_get_platform_type(&platform_type));
    if (HW_PLATFORM == platform_type)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_get_device_info(lchip, &device_info));
        if ((SYS_CHIP_DEVICE_ID_GB_5160 == device_info.device_id) ||
            (SYS_CHIP_DEVICE_ID_GB_5162 == device_info.device_id) ||
            (SYS_CHIP_DEVICE_ID_GB_5163 == device_info.device_id)||
            (SYS_CHIP_DEVICE_ID_RM_5120 == device_info.device_id)||
            (SYS_CHIP_DEVICE_ID_RT_3162 == device_info.device_id)||
            (SYS_CHIP_DEVICE_ID_RT_3163 == device_info.device_id))
        {
            ret = CTC_E_NONE;
        }
        else
        {
            ret = CTC_E_INVALID_DEVICE_ID;
        }
    }

    return ret;
}

int32
sys_greatbelt_chip_get_net_tx_cal_10g_entry(uint8 lchip, uint8* value)
{
    drv_datapath_master_t* p_datapath = NULL;
    CTC_ERROR_RETURN(drv_greatbelt_get_datapath(lchip, &p_datapath));

    if(0 == p_datapath->net_tx_cal_10g_value)
    {
        *value = 10;
    }
    else
    {
        *value = p_datapath->net_tx_cal_10g_value;
    }
    return CTC_E_NONE;
}

bool
sys_greatbelt_chip_check_feature_support(uint8 lchip, uint8 feature)
{

    if (gb_feature_support_arr[feature])
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

