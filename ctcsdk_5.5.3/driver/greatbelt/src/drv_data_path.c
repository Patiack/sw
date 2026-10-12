/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "greatbelt/include/drv_lib.h"
#include "greatbelt/include/drv_tbl_reg.h"
#include "greatbelt/include/drv_io.h"
#include "greatbelt/include/drv_error.h"
#include "greatbelt/include/drv_common.h"
#include "greatbelt/include/drv_data_path.h"
#include "greatbelt/include/drv_chip_ctrl.h"

#define DATAPATH_SLEEP_TIME 100

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define EMPTY_LINE(C)     ((C) == '\0' || (C) == '\r' || (C) == '\n')
#define WHITE_SPACE(C)    ((C) == '\t' || (C) == ' ')

STATIC int32
drv_greatbelt_datapath_read_chip_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_size_info(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_serdes_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_serdes_type_info(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_pll_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_bool_info(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_sync_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_calendar_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_net_tx_calendar_10g_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_misc_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_misc_type(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_nettxbufcfg_item(const char* line, void* para);
STATIC int32
drv_greatbelt_datapath_read_nettxbufcfg_info(const char* line, void* para);
STATIC int32
drv_greatbelt_set_hss12g_link_power_down(uint8 lchip, uint8 hssid, uint8 link_idx);
STATIC int32
drv_greatbelt_set_hss12g_link_power_up(uint8 lchip, uint8 hssid, uint8 link_idx, uint8 serdes_mode);
STATIC uint8
drv_greatbelt_get_free_chan(uint8 lchip);


/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
uint8 g_datapath_debug_on = 1;
static uint8 g_parse_idx = 0;
static uint8 g_parse_cnt = 0;
static uint8 g_parse_cnt_lport = 3;
static drv_chip_item_t g_gb_chip_para;

static drv_datapath_master_t gb_datapath_master[DRV_MAX_CHIP_NUM];
static drv_chip_item_type_t chip_item;
static uint32 value;
static drv_serdes_item_type_t serdes_item;
static drv_datapath_serdes_type_t serdes_type;
static drv_serdes_speed_t serdes_speed;
static uint32 serdes_chan;

static drv_pll_item_type_t pll_item;
static uint32 local_phy_port;
static uint32 is_used;
static uint32 pll_refclk;
static uint32 pll_output_a;
static uint32 pll_output_b;
static uint32 pll_cfg1;
static uint32 pll_cfg2;
static uint32 pll_cfg3;
static uint32 pll_cfg4;
static uint32 pll_cfg5;
static drv_sync_item_type_t sync_item;
static uint32 sync_cfg1;
static uint32 sync_cfg2;

static drv_calendar_item_type_t calendar_ptr_item;
static drv_calendar_item_type_t calendar_entry_item;
static drv_net_tx_calendar_10g_item_type_t net_tx_calendar_10g_entry_item;
static uint32 entry;

static drv_misc_item_type_t misc_item;
static drv_nettxbufcfg_item_type_t nettxbufcfg_item;

/* for chip infor parse */
drv_para_pair_t g_gb_chip_para_pair_t[] =
{
    {"[CHIP_ITEM]", drv_greatbelt_datapath_read_chip_item, &chip_item},
    {"[CHIP_SEQ]", drv_greatbelt_datapath_read_size_info, &value},
    {NULL, NULL, NULL}
};

/* for serdes infor parse */
drv_para_pair_t g_serdes_para_pair_t[] =
{
    {"[SERDES_ITEM]", drv_greatbelt_datapath_read_serdes_item, &serdes_item},
    {"[SERDES_MODE]", drv_greatbelt_datapath_read_serdes_type_info, &serdes_type},
    {"[SERDES_SPEED]", drv_greatbelt_datapath_read_size_info, &serdes_speed},
    {"[SERDES_CHANID]", drv_greatbelt_datapath_read_size_info, &serdes_chan},
    {"[SERDES_SWITCH]", drv_greatbelt_datapath_read_bool_info, &is_used},
    {NULL, NULL, NULL}
};

/* for pll infor parse */
drv_para_pair_t g_pll_para_pair_t[] =
{
    {"[PLL_ITEM]", drv_greatbelt_datapath_read_pll_item, &pll_item},
    {"[PLL_ENABLE]", drv_greatbelt_datapath_read_bool_info, &is_used},
    {"[PLL_REFCLK]", drv_greatbelt_datapath_read_size_info, &pll_refclk},
    {"[PLL_OUTPUTA]", drv_greatbelt_datapath_read_size_info, &pll_output_a},
    {"[PLL_OUTPUTB]", drv_greatbelt_datapath_read_size_info, &pll_output_b},
    {"[PLL_CFG1]", drv_greatbelt_datapath_read_size_info, &pll_cfg1},
    {"[PLL_CFG2]", drv_greatbelt_datapath_read_size_info, &pll_cfg2},
    {"[PLL_CFG3]", drv_greatbelt_datapath_read_size_info, &pll_cfg3},
    {"[PLL_CFG4]", drv_greatbelt_datapath_read_size_info, &pll_cfg4},
    {"[PLL_CFG5]", drv_greatbelt_datapath_read_size_info, &pll_cfg5},
    {NULL, NULL, NULL}
};

/* for syncE parse */
drv_para_pair_t g_sync_para_pair_t[] =
{
    {"[SYNCE_ITEM]", drv_greatbelt_datapath_read_sync_item, &sync_item},
    {"[SYNCE_ENABLE]", drv_greatbelt_datapath_read_bool_info, &is_used},
    {"[SYNCE_CFG1]", drv_greatbelt_datapath_read_size_info, &sync_cfg1},
    {"[SYNCE_CFG2]", drv_greatbelt_datapath_read_size_info, &sync_cfg2},
    {NULL, NULL, NULL}
};

/* for calendar ptr parse*/
drv_para_pair_t g_calendar_ptr_para_pair_t[] =
{
    {"[NETTXCALENDARPTR_ITEM]", drv_greatbelt_datapath_read_calendar_item, &calendar_ptr_item},
    {"[NETTXCALENDARPTR_VALUE]", drv_greatbelt_datapath_read_size_info, &value},
    {NULL, NULL, NULL}
};

/* for calendar entry parse*/
drv_para_pair_t g_calendar_entry_para_pair_t[] =
{
    {"[CALENDAR_ITEM]", drv_greatbelt_datapath_read_calendar_item, &calendar_entry_item},
    {"[CALENDAR_ENTRY]", drv_greatbelt_datapath_read_size_info, &entry},
    {"[CALENDAR_VALUE]", drv_greatbelt_datapath_read_size_info, &value},
    {NULL, NULL, NULL}
};

/* for  net tx calendar 10G parser  */
drv_para_pair_t g_net_tx_calendar_10g_entry_para_pair_t[] =
{
    {"[NETTXCALENDAR10G_ITEM]", drv_greatbelt_datapath_read_net_tx_calendar_10g_item, &net_tx_calendar_10g_entry_item},
    {"[NETTXCALENDAR10G_VALUE]", drv_greatbelt_datapath_read_size_info, &value},
    {NULL, NULL, NULL}
};

/* for misc parse*/
drv_para_pair_t g_misc_para_pair_t[] =
{
    {"[MISC_ITEM]", drv_greatbelt_datapath_read_misc_item, &misc_item},
    {"[MISC_USED]", drv_greatbelt_datapath_read_misc_type, &value},
    {NULL, NULL, NULL}
};

/* for nettxbufcfg parse*/
drv_para_pair_t g_nettxbufcfg_para_pair_t[] =
{
    {"[NETTXBUFCFG_ITEM]", drv_greatbelt_datapath_read_nettxbufcfg_item, &nettxbufcfg_item},
    {"[NETTXBUFCFG_USED]", drv_greatbelt_datapath_read_nettxbufcfg_info, &value},
    {NULL, NULL, NULL}
};

/* for mac capbility parse */
drv_para_pair_t g_mac_cap_para_pair_t[] =
{
    {"[MAC_MACID]", drv_greatbelt_datapath_read_size_info, &entry},
    {"[MAC_CHANID]", drv_greatbelt_datapath_read_size_info, &value},
    {"[LOCAL_PHYPORT]", drv_greatbelt_datapath_read_size_info, &local_phy_port},
    {"[MAC_USED]", drv_greatbelt_datapath_read_bool_info, &is_used},
    {NULL, NULL, NULL}
};
/****************************************************************************
 *
* Function
*
*****************************************************************************/

#define __READ_PROFILE

STATIC int32
drv_greatbelt_datapath_parse_chip_info( char* line)
{
    void* p_temp = NULL;

    while (g_gb_chip_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_gb_chip_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_gb_chip_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_gb_chip_para_pair_t[g_parse_idx].argus;

            g_gb_chip_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_gb_chip_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 2))
    {
        g_gb_chip_para.seq = (uint8)value;
        if (value >= DRV_MAX_CHIP_NUM)
        {
            DRV_DBG_INFO("Invalid chip id, value:%d \n", value);
        }

        g_parse_cnt = 0;
        chip_item = 0;
        value = 0;
    }

    g_parse_idx = 0;
    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_serdes_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_serdes_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_serdes_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_serdes_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_serdes_para_pair_t[g_parse_idx].argus;

            g_serdes_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_serdes_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 5))
    {
        gb_datapath_master[chip_id].serdes_cfg[serdes_item].mode =  (uint8)serdes_type.hss_lane_mode;
        gb_datapath_master[chip_id].serdes_cfg[serdes_item].mac_id = (uint8)serdes_type.mac_id;
        gb_datapath_master[chip_id].serdes_cfg[serdes_item].speed = (uint8)serdes_speed;
        gb_datapath_master[chip_id].serdes_cfg[serdes_item].chan_id = (uint8)serdes_chan;
        gb_datapath_master[chip_id].serdes_cfg[serdes_item].dynamic = (uint8)is_used;

        /*special for 2.5G*/
        if ((serdes_type.hss_lane_mode == DRV_SERDES_SGMII_MODE) && (serdes_speed == DRV_SERDES_SPPED_3DOT125G))
        {
            gb_datapath_master[chip_id].serdes_cfg[serdes_item].mode = DRV_SERDES_2DOT5_MODE;
        }

        g_parse_cnt = 0;
        serdes_item = 0;
        serdes_type.hss_lane_mode = 0;
        serdes_type.mac_id = 0;
        serdes_speed = 0;
        serdes_chan = 0;
        value = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_pll_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_pll_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_pll_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_pll_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_pll_para_pair_t[g_parse_idx].argus;

            g_pll_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    /* core pll */
    if (pll_item == DRV_PLL_ITEM_CORE)
    {
        if ((g_pll_para_pair_t[g_parse_idx].para_name != NULL) &&
            (g_parse_cnt == 10))
        {
            gb_datapath_master[chip_id].pll_cfg.core_pll.is_used = is_used;
            gb_datapath_master[chip_id].pll_cfg.core_pll.ref_clk = pll_refclk;
            gb_datapath_master[chip_id].pll_cfg.core_pll.output_a = pll_output_a;
            gb_datapath_master[chip_id].pll_cfg.core_pll.output_b = pll_output_b;
            gb_datapath_master[chip_id].pll_cfg.core_pll.core_pll_cfg1 = pll_cfg1;
            gb_datapath_master[chip_id].pll_cfg.core_pll.core_pll_cfg2 = pll_cfg2;
            gb_datapath_master[chip_id].pll_cfg.core_pll.core_pll_cfg3 = pll_cfg3;
            gb_datapath_master[chip_id].pll_cfg.core_pll.core_pll_cfg4 = pll_cfg4;
            gb_datapath_master[chip_id].pll_cfg.core_pll.core_pll_cfg5 = pll_cfg5;

            pll_item = 0;
            is_used = 0;
            pll_refclk = 0;
            pll_output_a = 0;
            pll_output_b = 0;
            pll_cfg1 = 0;
            pll_cfg2 = 0;
            pll_cfg3 = 0;
            pll_cfg4 = 0;
            pll_cfg5 = 0;
            g_parse_cnt = 0;

        }
    }
    else if (pll_item == DRV_PLL_ITEM_TANK)
    {
        /* tank pll */
        if ((g_pll_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 7))
        {
            gb_datapath_master[chip_id].pll_cfg.tank_pll.is_used = is_used;
            gb_datapath_master[chip_id].pll_cfg.tank_pll.ref_clk = pll_refclk;
            gb_datapath_master[chip_id].pll_cfg.tank_pll.output_a = pll_output_a;
            gb_datapath_master[chip_id].pll_cfg.tank_pll.output_b = pll_output_b;
            gb_datapath_master[chip_id].pll_cfg.tank_pll.tank_pll_cfg1 = pll_cfg1;
            gb_datapath_master[chip_id].pll_cfg.tank_pll.tank_pll_cfg2 = pll_cfg2;

            pll_item = 0;
            is_used = 0;
            pll_refclk = 0;
            pll_output_a = 0;
            pll_output_b = 0;
            pll_cfg1 = 0;
            pll_cfg2 = 0;
            g_parse_cnt = 0;
        }
    }
    else
    {
        if (pll_item < DRV_PLL_ITEM_HSS0)
        {
            return DRV_E_INVAILD_TYPE;
        }

        /* hss pll */
        if ((g_pll_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 4))
        {
            gb_datapath_master[chip_id].pll_cfg.hss_pll[pll_item-DRV_PLL_ITEM_HSS0].is_used = is_used;
            gb_datapath_master[chip_id].pll_cfg.hss_pll[pll_item-DRV_PLL_ITEM_HSS0].hss_pll_cfg1 = pll_cfg1;
            gb_datapath_master[chip_id].pll_cfg.hss_pll[pll_item-DRV_PLL_ITEM_HSS0].hss_pll_cfg2 = pll_cfg2;

            pll_item = 0;
            is_used = 0;
            pll_cfg1 = 0;
            pll_cfg2 = 0;
            g_parse_cnt = 0;
        }
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_sync_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_sync_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_sync_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_sync_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_sync_para_pair_t[g_parse_idx].argus;

            g_sync_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_sync_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 4))
    {

        g_parse_cnt = 0;
        gb_datapath_master[chip_id].sync_cfg[sync_item].is_used = is_used;
        gb_datapath_master[chip_id].sync_cfg[sync_item].sync_cfg1 = sync_cfg1;
        gb_datapath_master[chip_id].sync_cfg[sync_item].sync_cfg2 = sync_cfg2;

        is_used = 0;
        sync_cfg1 = 0;
        sync_cfg2 = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_cal_ptr_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_calendar_ptr_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_calendar_ptr_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_calendar_ptr_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_calendar_ptr_para_pair_t[g_parse_idx].argus;

            g_calendar_ptr_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_calendar_ptr_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 2))
    {

        if (calendar_ptr_item < DRV_CALENDAR_PTR_ITEM_SEL)
        {
            gb_datapath_master[chip_id].cal_infor[calendar_ptr_item].cal_walk_end = (uint8)value;
        }
        else
        {
            gb_datapath_master[chip_id].calendar_sel = (uint8)value;
        }

        g_parse_cnt = 0;
        value = 0;
        calendar_ptr_item = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_cal_entry_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_calendar_entry_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_calendar_entry_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_calendar_entry_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_calendar_entry_para_pair_t[g_parse_idx].argus;

            g_calendar_entry_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_calendar_entry_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 3))
    {

        if (entry >= 160)
        {
            return DRV_E_EXCEED_MAX_SIZE;
        }

        gb_datapath_master[chip_id].cal_infor[calendar_entry_item].net_tx_cal_entry[entry] = value;

        value = 0;
        calendar_entry_item = 0;
        g_parse_cnt = 0;
        entry = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_net_tx_cal_10g_entry_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_net_tx_calendar_10g_entry_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_net_tx_calendar_10g_entry_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_net_tx_calendar_10g_entry_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_net_tx_calendar_10g_entry_para_pair_t[g_parse_idx].argus;

            g_net_tx_calendar_10g_entry_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_net_tx_calendar_10g_entry_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 2))
    {
        if (net_tx_calendar_10g_entry_item == DRV_NET_TX_CALENDAR_10G_ENTRY_NUM)
        {
             gb_datapath_master[chip_id].net_tx_cal_10g_value = value;
        }

        value = 0;
        net_tx_calendar_10g_entry_item = 0;
        g_parse_cnt = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_misc_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_misc_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_misc_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_misc_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_misc_para_pair_t[g_parse_idx].argus;

            g_misc_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_misc_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 2))
    {
        if (misc_item == DRV_MISC_ITEM_CPU_MAC)
        {
            gb_datapath_master[chip_id].misc_info.cpumac_type = value;
        }
        else if (misc_item == DRV_MISC_ITEM_PTP_ENGINE)
        {
            gb_datapath_master[chip_id].misc_info.ptp_used = value;
        }
        else if (misc_item == DRV_MISC_ITEM_LED_DRIVER)
        {
            gb_datapath_master[chip_id].misc_info.mac_led_used = value;
        }

        g_parse_cnt = 0;
        value = 0;
        misc_item = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_nettxbufcfg_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;

    while (g_nettxbufcfg_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_nettxbufcfg_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_nettxbufcfg_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_nettxbufcfg_para_pair_t[g_parse_idx].argus;

            g_nettxbufcfg_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_nettxbufcfg_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == 2))
    {
        if (nettxbufcfg_item == DRV_NETTXBUFCFG_ITEM_1G)
        {
            gb_datapath_master[chip_id].nettxbuf_cfg.gmac_step = value;
        }
        else if (nettxbufcfg_item == DRV_NETTXBUFCFG_ITEM_SG)
        {
            gb_datapath_master[chip_id].nettxbuf_cfg.sgmac_step = value;
        }
        else if (nettxbufcfg_item == DRV_NETTXBUFCFG_ITEM_INTLK)
        {
            gb_datapath_master[chip_id].nettxbuf_cfg.intlk_step= value;
        }

        else if (nettxbufcfg_item == DRV_NETTXBUFCFG_ITEM_DXAUI)
        {
            gb_datapath_master[chip_id].nettxbuf_cfg.dxaui_step= value;
        }
        else if (nettxbufcfg_item == DRV_NETTXBUFCFG_ITEM_ELOOP)
        {
            gb_datapath_master[chip_id].nettxbuf_cfg.eloop_step= value;
        }
        else
        {}


        g_parse_cnt = 0;
        nettxbufcfg_item = 0;
        value = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_parse_mac_cap_info(char* line, uint8 chip_id)
{
    void* p_temp = NULL;
    if (sal_strncmp("[LOCAL_PHYPORT]", line, sal_strlen("[LOCAL_PHYPORT]")) == 0)
    {
        g_parse_cnt_lport = 4;
    }

    while (g_mac_cap_para_pair_t[g_parse_idx].para_name != NULL)
    {
        if (sal_strncmp(g_mac_cap_para_pair_t[g_parse_idx].para_name, line,
                        sal_strlen(g_mac_cap_para_pair_t[g_parse_idx].para_name)) == 0)
        {
            p_temp = g_mac_cap_para_pair_t[g_parse_idx].argus;

            g_mac_cap_para_pair_t[g_parse_idx].fun_ptr(line, p_temp);
            g_parse_cnt++;
            break;
        }

        g_parse_idx++;
    }

    if ((g_mac_cap_para_pair_t[g_parse_idx].para_name != NULL) &&
        (g_parse_cnt == g_parse_cnt_lport))
    {

        g_parse_cnt = 0;
        if (3 == g_parse_cnt_lport)
        {
            local_phy_port = entry;
        }

        if (is_used)
        {
            gb_datapath_master[chip_id].port_capability[local_phy_port].mac_id = entry;   /*parser mac id*/
            gb_datapath_master[chip_id].port_capability[local_phy_port].chan_id = value;  /*parser chan id*/
            gb_datapath_master[chip_id].port_capability[local_phy_port].valid = is_used;
			gb_datapath_master[chip_id].chan_used[value]  = is_used;
            if (entry < GMAC_NUM)
            {/*1G MAC must be 0~47*/

                gb_datapath_master[chip_id].port_capability[local_phy_port].port_type  = DRV_PORT_TYPE_1G;
            }
            else
            {/*10G MAC must be 48~59*/

                gb_datapath_master[chip_id].port_capability[local_phy_port].port_type  = DRV_PORT_TYPE_SG;
            }
        }
        is_used = 0;
        entry = 0;
        value = 0;
    }

    g_parse_idx = 0;

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_string_atrim(char* output, const char* input)
{
    char* p = NULL;

    DRV_PTR_VALID_CHECK(input);
    DRV_PTR_VALID_CHECK(output);

    /*trim left space*/
    while (*input != '\0')
    {
        if (WHITE_SPACE(*input))
        {
            ++input;
        }
        else
        {
            break;
        }
    }

    sal_strcpy(output, input);
    /*trim right space*/
    p = output + sal_strlen(output) - 1;

    while (p >= output)
    {
        /*skip empty line*/
        if (WHITE_SPACE(*p) || ('\r' == (*p)) || ('\n' == (*p)))
        {
            --p;
        }
        else
        {
            break;
        }
    }

    *(++p) = '\0';

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_bool_info(const char* line, void* para)
{
    char* ch = NULL;
    uint32* alloc_info = (uint32*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (0 == sal_strncmp(ch, "Y", sal_strlen("Y")))
    {
        *alloc_info = 1;
    }
    else if (0 == sal_strncmp(ch, "N", sal_strlen("N")))
    {
        *alloc_info = 0;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_size_info(const char* line, void* para)
{
    uint32 total = 0;
    uint32* mem_size = (uint32*)para;
    char* ch = NULL;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (!sal_memcmp(ch, "0x", 2) || !sal_memcmp(ch, "0X", 2))
    {
        total = sal_strtou32(ch, NULL, 16);

        *mem_size = total;
    }
    else if (!sal_memcmp(ch, "NA", 2))
    {
        *mem_size = 0;
    }
    else
    {
        sal_sscanf(ch, "%u", mem_size);
    }

    return 0;
}

STATIC int32
drv_greatbelt_datapath_read_chip_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_chip_item_type_t* chip_item = (drv_chip_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "ChipId", sal_strlen("ChipId")) == 0)
    {
        *chip_item = DRV_CHIP_ITEM_TYPE_CHIP_ID;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_serdes_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_serdes_item_type_t* serdes_item = (drv_serdes_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVALID_PTR;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "Serdes10", sal_strlen("Serdes10")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG10;
    }
    else if (sal_strncmp(ch, "Serdes11", sal_strlen("Serdes11")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG11;
    }
    else if (sal_strncmp(ch, "Serdes12", sal_strlen("Serdes12")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG12;
    }
    else if (sal_strncmp(ch, "Serdes13", sal_strlen("Serdes13")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG13;
    }
    else if (sal_strncmp(ch, "Serdes14", sal_strlen("Serdes14")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG14;
    }
    else if (sal_strncmp(ch, "Serdes15", sal_strlen("Serdes15")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG15;
    }
    else if (sal_strncmp(ch, "Serdes16", sal_strlen("Serdes16")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG16;
    }
    else if (sal_strncmp(ch, "Serdes17", sal_strlen("Serdes17")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG17;
    }
    else if (sal_strncmp(ch, "Serdes18", sal_strlen("Serdes18")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG18;
    }
    else if (sal_strncmp(ch, "Serdes19", sal_strlen("Serdes19")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG19;
    }
    else if (sal_strncmp(ch, "Serdes20", sal_strlen("Serdes20")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG20;
    }
    else if (sal_strncmp(ch, "Serdes21", sal_strlen("Serdes21")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG21;
    }
    else if (sal_strncmp(ch, "Serdes22", sal_strlen("Serdes22")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG22;
    }
    else if (sal_strncmp(ch, "Serdes23", sal_strlen("Serdes23")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG23;
    }
    else if (sal_strncmp(ch, "Serdes24", sal_strlen("Serdes24")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG24;
    }
    else if (sal_strncmp(ch, "Serdes25", sal_strlen("Serdes25")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG25;
    }
    else if (sal_strncmp(ch, "Serdes26", sal_strlen("Serdes26")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG26;
    }
    else if (sal_strncmp(ch, "Serdes27", sal_strlen("Serdes27")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG27;
    }
    else if (sal_strncmp(ch, "Serdes28", sal_strlen("Serdes28")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG28;
    }
    else if (sal_strncmp(ch, "Serdes29", sal_strlen("Serdes29")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG29;
    }
    else if (sal_strncmp(ch, "Serdes30", sal_strlen("Serdes30")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG30;
    }
    else if (sal_strncmp(ch, "Serdes31", sal_strlen("Serdes31")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG31;
    }


    else if (sal_strncmp(ch, "Serdes0", sal_strlen("Serdes0")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG0;
    }
    else if (sal_strncmp(ch, "Serdes1", sal_strlen("Serdes1")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG1;
    }
    else if (sal_strncmp(ch, "Serdes2", sal_strlen("Serdes2")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG2;
    }
    else if (sal_strncmp(ch, "Serdes3", sal_strlen("Serdes3")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG3;
    }
    else if (sal_strncmp(ch, "Serdes4", sal_strlen("Serdes4")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG4;
    }
    else if (sal_strncmp(ch, "Serdes5", sal_strlen("Serdes5")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG5;
    }
    else if (sal_strncmp(ch, "Serdes6", sal_strlen("Serdes6")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG6;
    }
    else if (sal_strncmp(ch, "Serdes7", sal_strlen("Serdes7")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG7;
    }
    else if (sal_strncmp(ch, "Serdes8", sal_strlen("Serdes8")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG8;
    }
    else if (sal_strncmp(ch, "Serdes9", sal_strlen("Serdes9")) == 0)
    {
        *serdes_item = DRV_SERDES_ITEM_NTSG9;
    }
    else
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_serdes_type_info(const char* line, void* para)
{
    char* ch = NULL;
    drv_datapath_serdes_type_t* serdes_type = (drv_datapath_serdes_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVALID_PTR;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "QSgmii", sal_strlen("QSgmii")) == 0)
    {
        serdes_type->hss_lane_mode = DRV_SERDES_QSGMII_MODE;
        ch += sal_strlen("QSgmii");
        serdes_type->mac_id = sal_strtou32(ch, NULL, 10)*4;
    }
    else if (sal_strncmp(ch, "Sgmii", sal_strlen("Sgmii")) == 0)
    {
        serdes_type->hss_lane_mode = DRV_SERDES_SGMII_MODE;
        ch += sal_strlen("Sgmii");
        serdes_type->mac_id = sal_strtou32(ch, NULL, 10);
    }
    else if (sal_strncmp(ch, "XFI", sal_strlen("XFI")) == 0)
    {
        serdes_type->hss_lane_mode = DRV_SERDES_XFI_MODE;
        ch += sal_strlen("XFI");
        serdes_type->mac_id = sal_strtou32(ch, NULL, 10)+48;
    }
    else if (sal_strncmp(ch, "XAUI", sal_strlen("XAUI")) == 0)
    {
        serdes_type->hss_lane_mode = DRV_SERDES_XAUI_MODE;
        ch += sal_strlen("XAUI");
        serdes_type->mac_id = sal_strtou32(ch, NULL, 10)+48;
    }
    else if (sal_strncmp(ch, "IntLk", sal_strlen("IntLk")) == 0)
    {
        serdes_type->hss_lane_mode = DRV_SERDES_INTLK_MODE;
        ch += sal_strlen("IntLk");
        serdes_type->mac_id = sal_strtou32(ch, NULL, 10);
    }
    else if (sal_strncmp(ch, "NA", sal_strlen("NA")) == 0)
    {
        serdes_type->hss_lane_mode = DRV_SERDES_NONE_MODE;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_pll_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_pll_item_type_t* pll_item = (drv_pll_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "Core", sal_strlen("Core")) == 0)
    {
        *pll_item = DRV_PLL_ITEM_CORE;
    }
    else if (sal_strncmp(ch, "Tank", sal_strlen("Tank")) == 0)
    {
        *pll_item = DRV_PLL_ITEM_TANK;
    }
    else if (sal_strncmp(ch, "HSS0", sal_strlen("HSS0")) == 0)
    {
        *pll_item = DRV_PLL_ITEM_HSS0;
    }
    else if (sal_strncmp(ch, "HSS1", sal_strlen("HSS1")) == 0)
    {
        *pll_item = DRV_PLL_ITEM_HSS1;
    }
    else if (sal_strncmp(ch, "HSS2", sal_strlen("HSS2")) == 0)
    {
        *pll_item = DRV_PLL_ITEM_HSS2;
    }
    else if (sal_strncmp(ch, "HSS3", sal_strlen("HSS3")) == 0)
    {
        *pll_item = DRV_PLL_ITEM_HSS3;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}


STATIC int32
drv_greatbelt_datapath_read_sync_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_sync_item_type_t* mdio_item = (drv_sync_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "SyncE0", sal_strlen("SyncE0")) == 0)
    {
        *mdio_item = DRV_SYNCE_ITEM_ITEM0;
    }
    else if (sal_strncmp(ch, "SyncE1", sal_strlen("SyncE1")) == 0)
    {
        *mdio_item = DRV_SYNCE_ITEM_ITEM1;
    }
    else if (sal_strncmp(ch, "SyncE2", sal_strlen("SyncE2")) == 0)
    {
        *mdio_item = DRV_SYNCE_ITEM_ITEM2;
    }
    else if (sal_strncmp(ch, "SyncE3", sal_strlen("SyncE3")) == 0)
    {
        *mdio_item = DRV_SYNCE_ITEM_ITEM3;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}


STATIC int32
drv_greatbelt_datapath_read_calendar_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_calendar_item_type_t* calendar_item = (drv_calendar_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "calCtl", sal_strlen("calCtl")) == 0)
    {
        *calendar_item = DRV_CALENDAR_PTR_ITEM_CTL;
    }
    else if (sal_strncmp(ch, "calBak", sal_strlen("calBak")) == 0)
    {
        *calendar_item = DRV_CALENDAR_PTR_ITEM_BAK;
    }
    else if (sal_strncmp(ch, "calSel", sal_strlen("calSel")) == 0)
    {
        *calendar_item = DRV_CALENDAR_PTR_ITEM_SEL;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_net_tx_calendar_10g_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_net_tx_calendar_10g_item_type_t* calendar_item = (drv_net_tx_calendar_10g_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "entryNum", sal_strlen("entryNum")) == 0)
    {
        *calendar_item = DRV_NET_TX_CALENDAR_10G_ENTRY_NUM;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}


STATIC int32
drv_greatbelt_datapath_read_misc_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_misc_item_type_t* misc_item = (drv_misc_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }


    if (sal_strncmp(ch, "CpuMac", sal_strlen("CpuMac")) == 0)
    {
        *misc_item = DRV_MISC_ITEM_CPU_MAC;
    }
    else if (sal_strncmp(ch, "PtpEngine", sal_strlen("PtpEngine")) == 0)
    {
        *misc_item = DRV_MISC_ITEM_PTP_ENGINE;
    }
    else if (sal_strncmp(ch, "LedDriver", sal_strlen("LedDriver")) == 0)
    {
        *misc_item = DRV_MISC_ITEM_LED_DRIVER;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_misc_type(const char* line, void* para)
{
    char* ch = NULL;
    drv_cpumac_type_t * cpumac_type = NULL;
    uint32* temp = NULL;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVALID_PTR;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (misc_item == DRV_MISC_ITEM_CPU_MAC)
    {
        cpumac_type = (drv_cpumac_type_t*)para;

        if (sal_strncmp(ch, "10M", sal_strlen("10M")) == 0)
        {
            *cpumac_type = DRV_CPU_MAC_TYPE_10M;
        }
        else if (sal_strncmp(ch, "100M", sal_strlen("100M")) == 0)
        {
            *cpumac_type = DRV_CPU_MAC_TYPE_100M;
        }
        else if (sal_strncmp(ch, "GE", sal_strlen("GE")) == 0)
        {
            *cpumac_type = DRV_CPU_MAC_TYPE_GE;
        }
        else if (sal_strncmp(ch, "NA", sal_strlen("NA")) == 0)
        {
            *cpumac_type = DRV_CPU_MAC_TYPE_NA;
        }
    }
    else if (misc_item == DRV_MISC_ITEM_PTP_ENGINE)
    {
        temp = (uint32*)para;
        if (0 == sal_strncmp(ch, "Y", sal_strlen("Y")))
        {
            *temp = 1;
        }
        else if (0 == sal_strncmp(ch, "N", sal_strlen("N")))
        {
            *temp = 0;
        }
    }
    else if (misc_item == DRV_MISC_ITEM_LED_DRIVER)
    {
        temp = (uint32*)para;
        if (0 == sal_strncmp(ch, "Y", sal_strlen("Y")))
        {
            *temp = 1;
        }
        else if (0 == sal_strncmp(ch, "N", sal_strlen("N")))
        {
            *temp = 0;
        }
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_nettxbufcfg_item(const char* line, void* para)
{
    char* ch = NULL;
    drv_nettxbufcfg_item_type_t* nettxbufcfg_item = (drv_nettxbufcfg_item_type_t*)para;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (sal_strncmp(ch, "GE", sal_strlen("GE")) == 0)
    {
        *nettxbufcfg_item = DRV_NETTXBUFCFG_ITEM_1G;
    }
    else if (sal_strncmp(ch, "10GE", sal_strlen("10GE")) == 0)
    {
        *nettxbufcfg_item = DRV_NETTXBUFCFG_ITEM_SG;
    }
    else if (sal_strncmp(ch, "Interlaken", sal_strlen("Interlaken")) == 0)
    {
        *nettxbufcfg_item = DRV_NETTXBUFCFG_ITEM_INTLK;
    }
    else if (sal_strncmp(ch, "DXAUI", sal_strlen("DXAUI")) == 0)
    {
        *nettxbufcfg_item = DRV_NETTXBUFCFG_ITEM_DXAUI;
    }
    else if (sal_strncmp(ch, "Eloop", sal_strlen("Eloop")) == 0)
    {
        *nettxbufcfg_item = DRV_NETTXBUFCFG_ITEM_ELOOP;
    }
    else
    {
        return DRV_E_INVAILD_TYPE;
    }

    return DRV_E_NONE;
}

STATIC int32
drv_greatbelt_datapath_read_nettxbufcfg_info(const char* line, void* para)
{
    uint32* step = (uint32*)para;
    char* ch = NULL;

    DRV_PTR_VALID_CHECK(line);
    DRV_PTR_VALID_CHECK(para);

    ch = sal_strstr(line, "=");
    if (NULL == ch)
    {
        return DRV_E_INVAILD_TYPE;
    }
    else
    {
        ch++;
    }

    while (sal_isspace((unsigned char)*ch))
    {
        ch++;
    }

    if (!sal_memcmp(ch, "0x", 2) || !sal_memcmp(ch, "0X", 2))
    {
        *step = sal_strtou32(ch, NULL, 16);
    }
    else if (!sal_memcmp(ch, "NA", 2))
    {
        *step = 0;
    }
    else
    {
        sal_sscanf(ch, "%u", step);
    }

    return DRV_E_NONE;

}

int32
drv_greatbelt_read_datapath_profile(const char* file_name)
{
    char    string[128];
    char    line[128];
    sal_file_t local_fp = NULL;
    int32   ret = DRV_E_NONE;

    DRV_PTR_VALID_CHECK(file_name);

    /*OPEN FILE*/
    local_fp = sal_fopen(file_name, "r");

    if (NULL == local_fp)
    {

        return DRV_E_INVAILD_TYPE;
    }

    /*parse profile*/
    while (NULL != sal_fgets(string, 128, local_fp))
    {
        /*comment line*/
        if ('#' == string[0])
        {
            continue;
        }

        /*trim left and right space*/
        sal_memset(line, 0, sizeof(line));

        ret = drv_greatbelt_datapath_string_atrim(line, string);

        if (EMPTY_LINE(line[0]))
        {
            continue;
        }

        drv_greatbelt_datapath_parse_chip_info(line);
        drv_greatbelt_datapath_parse_serdes_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_pll_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_sync_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_cal_ptr_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_cal_entry_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_net_tx_cal_10g_entry_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_misc_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_nettxbufcfg_info(line, g_gb_chip_para.seq);
        drv_greatbelt_datapath_parse_mac_cap_info(line, g_gb_chip_para.seq);

    }

    /*close file*/
    sal_fclose(local_fp);
    local_fp = NULL;

    return ret;
}

#define __READ_PROFILE_END

int32
drv_greatbelt_datapath_global_init(void)
{
    uint32 index = 0;
    uint8 lchip = 0;

    for (lchip = 0; lchip < DRV_MAX_CHIP_NUM; lchip++)
    {
        if (gb_datapath_master[lchip].init_flag)
        {
            continue;
        }

        sal_memset(&gb_datapath_master[lchip], 0, sizeof(drv_datapath_master_t));

        /* do not ignore fault by default */
        gb_datapath_master[lchip].ignore_mode = 0;

        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac6 = 0xff;
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac7 = 0xff;
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac8 = 0xff;
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac9 = 0xff;
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac10 = 0xff;
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac11 = 0xff;
        gb_datapath_master[lchip].pll_cfg.core_pll.output_a = 450;
        sal_memset(gb_datapath_master[lchip].serdes_cfg, 0xff, HSS_SERDES_LANE_NUM*sizeof(drv_datapath_serdes_info_t));


        /* channel default cfg */
        for (index = 0; index < GMAC_NUM; index++)
        {
            /* gmac 0~47 mapping to channid 0~47 */
            gb_datapath_master[lchip].gmac_cfg[index].chan_id = index;
        }

        /* init 8 10G mac  */
        for (index = 0; index < SGMAC_NUM; index++)
        {
            /* sgmac 0~11 mapping to channel id 48~59 */
            gb_datapath_master[lchip].sgmac_cfg[index].chan_id = GMAC_NUM + index;
        }

        /* init channel for local phy port capability */
        for (index = 0; index < MAX_LOCAL_PORT_NUM; index++)
        {
            if (index < MAC_NUM)
            {
                gb_datapath_master[lchip].port_capability[index].chan_id   = index;
            }
        }

        /* init reserved channel for local phy port capability */
        /* iloop channel */
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_ILOOP_LPORT].valid   = 1;
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_ILOOP_LPORT].chan_id = DRV_DATA_PATH_ILOOP_DEF_CHAN;
        gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_ILOOP_DEF_CHAN] = 1;

        /* cpu channel */
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_CPU_LPORT].valid     = 1;
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_CPU_LPORT].port_type = DRV_PORT_TYPE_CPU;
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_CPU_LPORT].chan_id   = DRV_DATA_PATH_CPU_DEF_CHAN;
        gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_CPU_DEF_CHAN] = 1;

        /* dma channel */
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_DMA_LPORT].valid     = 1;
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_DMA_LPORT].port_type = DRV_PORT_TYPE_DMA;
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_DMA_LPORT].chan_id   = DRV_DATA_PATH_DMA_DEF_CHAN;
        gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_DMA_DEF_CHAN] = 1;

        /* eloop channel */
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_ELOOP_LPORT].valid   = 1;
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_ELOOP_LPORT].chan_id = DRV_DATA_PATH_ELOOP_DEF_CHAN;
        gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_ELOOP_DEF_CHAN] = 1;
        /* it is fixed */
        gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_ELOOP_LPORT].mac_id = ELOOP_NETTX_PORT_ID;

        gb_datapath_master[lchip].init_flag = 1;
     }

    return DRV_E_NONE;
}

int32
drv_greatbelt_datapath_init_mac_softtb(uint8 chip_id)
{
    uint8 index = 0;
    uint8 mac_index = 0;
    uint8 mac_id = 0;
    uint8 serdes_type = 0;
    uint8 chan_id = 0;

    for (index = 0; index < HSS_SERDES_LANE_NUM; index++)
    {
        serdes_type = gb_datapath_master[chip_id].serdes_cfg[index].mode;
        mac_id = gb_datapath_master[chip_id].serdes_cfg[index].mac_id;
        chan_id = gb_datapath_master[chip_id].serdes_cfg[index].chan_id;

        if (DRV_SERDES_NONE_MODE == serdes_type)
        {
            /* serdes is not used */
             /*-DRV_DBG_INFO("Serdes%d is not used! \n", index);*/
            continue;
        }

        switch (serdes_type)
        {
            case DRV_SERDES_SGMII_MODE:

                if (mac_id >= GMAC_NUM)
                {
                    DRV_DBG_INFO("gmac id exceed max mac value!! \n");
                    return DRV_E_EXCEED_MAX_SIZE;
                }

                mac_index = mac_id;

                gb_datapath_master[chip_id].gmac_cfg[mac_index].valid = 1;
                gb_datapath_master[chip_id].gmac_cfg[mac_index].pcs_mode = DRV_SERDES_SGMII_MODE;
                gb_datapath_master[chip_id].gmac_cfg[mac_index].serdes_id = index;
                gb_datapath_master[chip_id].gmac_cfg[mac_index].chan_id = chan_id;

                break;

            case DRV_SERDES_QSGMII_MODE:
                if (mac_id >= QUADMAC_NUM*4)
                {
                    DRV_DBG_INFO("quadmac id exceed max mac value!! \n");
                    return DRV_E_EXCEED_MAX_SIZE;
                }

                mac_index = mac_id/4;

                gb_datapath_master[chip_id].quadmac_cfg[mac_index].valid = 1;
                gb_datapath_master[chip_id].quadmac_cfg[mac_index].pcs_mode = DRV_SERDES_QSGMII_MODE;
                gb_datapath_master[chip_id].quadmac_cfg[mac_index].serdes_id = index;
                gb_datapath_master[chip_id].quadmac_cfg[mac_index].chan_id = chan_id;

                break;

            case DRV_SERDES_XAUI_MODE:
                if ((mac_id - 48) >= SGMAC_NUM)
                {
                    DRV_DBG_INFO("sgmac id exceed max mac value!!Xaui mode \n");
                    return DRV_E_EXCEED_MAX_SIZE;
                }

                if (mac_id < 48)
                {
                    DRV_DBG_INFO("sgmac id less than 48!!Xaui mode \n");
                    return DRV_E_BIT_OUT_RANGE;
                }

                mac_index = mac_id - 48;

                if (gb_datapath_master[chip_id].sgmac_cfg[mac_index].valid)
                {
                    if (gb_datapath_master[chip_id].sgmac_cfg[mac_index].serdes_id > index)
                    {
                        /* xaui mode, sgmac ref serdes is begining of 4 serdes */
                        gb_datapath_master[chip_id].sgmac_cfg[mac_index].serdes_id = index;
                    }
                }
                else
                {
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].valid = 1;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].pcs_mode = DRV_SERDES_XAUI_MODE;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].serdes_id = index;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].chan_id = chan_id;

                }

                break;

            case DRV_SERDES_XGSGMII_MODE:
                if (mac_id - 48  >= SGMAC_NUM)
                {
                    DRV_DBG_INFO("sgmac id exceed max mac value!!XGSGMII mode \n");
                    return DRV_E_EXCEED_MAX_SIZE;
                }

                if (mac_id < 48)
                {
                    DRV_DBG_INFO("sgmac id less than 48!!XGSGMII mode \n");
                    return DRV_E_BIT_OUT_RANGE;
                }

                 mac_index = mac_id - 48;

                gb_datapath_master[chip_id].sgmac_cfg[mac_index].valid = 1;
                gb_datapath_master[chip_id].sgmac_cfg[mac_index].pcs_mode = DRV_SERDES_XGSGMII_MODE;
                gb_datapath_master[chip_id].sgmac_cfg[mac_index].serdes_id = index;
                gb_datapath_master[chip_id].sgmac_cfg[mac_index].chan_id = chan_id;

                break;

            case DRV_SERDES_XFI_MODE:
                if (mac_id - 48 >= SGMAC_NUM)
                {
                    DRV_DBG_INFO("sgmac id exceed max mac value!!Xfi mode \n");
                    return DRV_E_EXCEED_MAX_SIZE;
                }

                if (mac_id < 48)
                {
                    DRV_DBG_INFO("sgmac id less than 48!!Xfi mode \n");
                    return DRV_E_BIT_OUT_RANGE;
                }

                mac_index = mac_id - 48;

                gb_datapath_master[chip_id].sgmac_cfg[mac_index].valid = 1;
                gb_datapath_master[chip_id].sgmac_cfg[mac_index].pcs_mode = DRV_SERDES_XFI_MODE;
                gb_datapath_master[chip_id].sgmac_cfg[mac_index].serdes_id = index;
                gb_datapath_master[chip_id].sgmac_cfg[mac_index].chan_id = chan_id;

                break;

            case DRV_SERDES_2DOT5_MODE:
                if (mac_id < 48)
                {
                    mac_index = mac_id;
                    gb_datapath_master[chip_id].gmac_cfg[mac_index].valid = 1;
                    gb_datapath_master[chip_id].gmac_cfg[mac_index].pcs_mode = DRV_SERDES_SGMII_MODE;
                    gb_datapath_master[chip_id].gmac_cfg[mac_index].serdes_id = index;
                    gb_datapath_master[chip_id].gmac_cfg[mac_index].chan_id = chan_id;
                }
                else
                {
                    if (mac_id - 48 >= SGMAC_NUM)
                    {
                        DRV_DBG_INFO("sgmac id exceed max mac value!!Xfi mode \n");
                        return DRV_E_EXCEED_MAX_SIZE;
                    }

                    mac_index = mac_id - 48;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].valid = 1;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].pcs_mode = DRV_SERDES_XGSGMII_MODE;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].serdes_id = index;
                    gb_datapath_master[chip_id].sgmac_cfg[mac_index].chan_id = chan_id;
                }
                break;

            case DRV_SERDES_INTLK_MODE:
                break;
            default:
                break;
        }



    }

    return DRV_E_NONE;
}



int32
drv_greatbelt_datapath_init_internal_chan(uint8 lchip)
{
    uint8 channel_id= 0;

    /* init oam channel*/
	if(gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_OAM_DEF_CHAN])
	{
        channel_id = INVALID_CHAN_ID;
    }
	else
	{
	    channel_id = DRV_DATA_PATH_OAM_DEF_CHAN;
        gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_OAM_DEF_CHAN] = 1;
	}
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_OAM_LPORT].chan_id  =  channel_id;
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_OAM_LPORT].valid    = (channel_id != INVALID_CHAN_ID);

	 /* init interlaken channel*/
	if(gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_INTLK_DEF_CHAN])
	{
        channel_id = INVALID_CHAN_ID;
	}
	else
	{
	    channel_id = DRV_DATA_PATH_INTLK_DEF_CHAN;
        gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_INTLK_DEF_CHAN] = 1;
	}
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_INTLK_LPORT].chan_id  =  channel_id;
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_INTLK_LPORT].valid    = (channel_id != INVALID_CHAN_ID);
    /* it is fixed */
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_INTLK_LPORT].mac_id = INTERLAKEN_NETTX_PORT_ID;


    /* init drop channel*/
	if(gb_datapath_master[lchip].chan_used[DRV_DATA_PATH_DROP_DEF_CHAN])
	{
        channel_id = drv_greatbelt_get_free_chan(lchip);
	}
	else
	{
	    channel_id = DRV_DATA_PATH_DROP_DEF_CHAN;
	}
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_DROP_LPORT].chan_id  =  channel_id;
    gb_datapath_master[lchip].port_capability[DRV_DATA_PATH_DROP_LPORT].valid    = (channel_id != INVALID_CHAN_ID);



	return DRV_E_NONE;
}

/* lane_idx : 0xff, means all lane in one hss macro */
int32
drv_greatbelt_datapath_get_hss_internel_reg_cfg(uint8 lchip, uint8 hss_idx, uint8 serdes_idx,
                    uint32* p_tx_cfg, uint32* p_rx_cfg)
{
    uint32  tx_cfg = 0;
    uint32 rx_cfg = 0;
    uint8   sub_idx = 0;

    if (HSS_MACRO_NUM <= hss_idx)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    if ((0xff != serdes_idx) && (serdes_idx >= HSS_SERDES_LANE_NUM))
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    if (0xff == serdes_idx)
    {
        /* means broadcast, this mode cannot be mix mode */
        switch (gb_datapath_master[lchip].hss_cfg.hss_mode[hss_idx])
        {
            case DRV_SERDES_SGMII_MODE:
                tx_cfg = 0x00000003;
                rx_cfg = 0x00000047;
                break;

            case DRV_SERDES_QSGMII_MODE:
                tx_cfg = 0x00000003;
                rx_cfg = 0x00000045;
                break;

            case DRV_SERDES_XFI_MODE:
                tx_cfg = 0x00000000;
                rx_cfg = 0x0000000c;
                break;

            case DRV_SERDES_XAUI_MODE:
                break;

            case DRV_SERDES_INTLK_MODE:
                break;

            case DRV_SERDES_MIX_MODE_WITH_QSGMII:
            case DRV_SERDES_MIX_MODE_WITH_XFI:
            default:
                return DRV_E_DATAPATH_INVALID_HSS_MODE;
        }
    }
    else
    {
        switch(gb_datapath_master[lchip].serdes_cfg[serdes_idx].mode)
        {
            case DRV_SERDES_SGMII_MODE:
            case DRV_SERDES_XGSGMII_MODE:
                if ((gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed == DRV_SERDES_SPPED_1DOT25G) &&
                    (gb_datapath_master[lchip].pll_cfg.hss_pll[serdes_idx/8].aux_sel))
                {
                    /* sel auxclk */
                    tx_cfg = 0x00000027;
                    rx_cfg = 0x000000c7;
                    if (DRV_SERDES_MIX_MODE_WITH_QSGMII == gb_datapath_master[lchip].hss_cfg.hss_mode[serdes_idx/8])
                    {
                        tx_cfg = 0x00000003;
                        rx_cfg = 0x00000047;
                    }
                }
                else
                {
                    /* Rx: use 1/8 rate, databus width use 10 bit para; Tx: use 1/8 rate; not use externel C16 clock */
                    tx_cfg = 0x00000003;
                    rx_cfg = 0x00000047;
                }
                break;

            case DRV_SERDES_QSGMII_MODE:
                /* Rx: use 1/2 rate, databus width use 10 bit para, No DFE, Tx: use 1/2 rate; not use externel C16 clock */
                tx_cfg = 0x00000001;
                rx_cfg = 0x00000045;
                break;
            case DRV_SERDES_XFI_MODE:
                /* Rx: use full rate, databus width use 20 bit para; Tx: use full rate; not use externel C16 clock */
                tx_cfg = 0x00000000;
                rx_cfg = 0x0000000c;
                break;
            case DRV_SERDES_XAUI_MODE:
                for (sub_idx = 0; sub_idx < 8; sub_idx++)
                {
                    if (DRV_SERDES_SPPED_3DOT125G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        /* Rx: use 1/4 rate, databus width use 10 bit para; Tx: use 1/4 rate; not use externel C16 clock */
                        tx_cfg = 0x00000002;
                        rx_cfg = 0x00000046;
                    }
                    else if (DRV_SERDES_SPPED_5G15625G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        /* Rx: use 1/2 rate, databus width use 10 bit para; Tx: use 1/2 rate; not use externel C16 clock */
                        tx_cfg = 0x00000001;
                        rx_cfg = 0x00000005;
                    }
                    else if (DRV_SERDES_SPPED_6DOT25G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        /* Rx: use 1/2 rate, databus width use 10 bit para; Tx: use 1/2 rate; not use externel C16 clock */
                        tx_cfg = 0x00000001;
                        rx_cfg = 0x00000005;
                    }
                }
                break;

            case DRV_SERDES_INTLK_MODE:
                for (sub_idx = 0; sub_idx < 8; sub_idx++)
                {
                    if (DRV_SERDES_SPPED_3DOT125G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        /* Rx: use 1/4 rate, databus width use 20 bit para; Tx: use 1/4 rate; not use externel C16 clock */
                        tx_cfg = 0x00000002;
                        rx_cfg = 0x0000000e;
                    }
                    else if (DRV_SERDES_SPPED_6DOT25G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        /* Rx: use 1/2 rate, databus width use 20 bit para; Tx: use 1/2 rate; not use externel C16 clock */
                        tx_cfg = 0x00000001;
                        rx_cfg = 0x0000000d;
                    }
                    else if (DRV_SERDES_SPPED_10DOT3125G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        /* Rx: use FULL rate, databus width use 20 bit para; Tx: use 1/1 rate; not use externel C16 clock */
                        tx_cfg = 0x00000000;
                        rx_cfg = 0x0000000c;
                    }
                }
                break;

            case DRV_SERDES_2DOT5_MODE:
                /* Rx: use 1/4 rate, databus width use 20 bit para; Tx: use 1/4 rate; not use externel C16 clock */
                tx_cfg = 0x00000002;
                rx_cfg = 0x00000046;
                break;

            default:
                return DRV_E_DATAPATH_INVALID_HSS_MODE;
        }
    }

    *p_tx_cfg = tx_cfg;
    *p_rx_cfg = rx_cfg;

    return DRV_E_NONE;
}

int32
drv_greatbelt_pll_sgmac_init(uint8 lchip)
{
    uint32 sgmac_mode_cfg = 0;
    uint32 index = 0;
    drv_datapath_sgmac_info_t sgmac_info;

    for (index = 0; index < SGMAC_NUM; index++)
    {
        sal_memcpy(&sgmac_info, &gb_datapath_master[lchip].sgmac_cfg[index], sizeof(drv_datapath_sgmac_info_t));

        /* cfgSgmacXfiDis */
        if ((sgmac_info.valid) && (sgmac_info.pcs_mode != DRV_SERDES_XFI_MODE))
        {
            sgmac_mode_cfg |= (0x1 << (16+index));
        }
    }

    if (gb_datapath_master[lchip].sgmac_cfg[11].valid)
    {
        if (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac11 == 0)
        {
            /* sgmac11 select hss0 */
            sgmac_mode_cfg |= (1<<13);
            sgmac_mode_cfg &= ~((uint32)1<<11);
        }
        else if (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac11 == 1)
        {
            /* sgmac11 select hss1 */
            sgmac_mode_cfg &= ~((uint32)1<<13);
            sgmac_mode_cfg &= ~((uint32)1<<11);
        }
        else if (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac11 == 2)
        {
            /* sgmac11 select hss2 */
            sgmac_mode_cfg &= ~((uint32)1<<13);
            sgmac_mode_cfg |= (1<<11);
        }
    }

    if (gb_datapath_master[lchip].sgmac_cfg[10].valid)
    {
        if (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac10 == 0)
        {
            /* sgmac10 select hss0 */
            sgmac_mode_cfg |= (1<<12);
            sgmac_mode_cfg &= ~((uint32)1<<10);
        }
        else if (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac10 == 1)
        {
            /* sgmac10 select hss1 */
            sgmac_mode_cfg &= ~((uint32)1<<12);
            sgmac_mode_cfg &= ~((uint32)1<<10);
        }
        else if (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac10 == 2)
        {
            /* sgmac10 select hss2 */
            sgmac_mode_cfg &= ~((uint32)1<<12);
            sgmac_mode_cfg |= (1<<10);
        }
    }

    if ((gb_datapath_master[lchip].sgmac_cfg[9].valid) &&
        (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac9 == 2))
    {
        sgmac_mode_cfg |= (1<<9);
    }

    if ((gb_datapath_master[lchip].sgmac_cfg[8].valid) &&
        (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac8 == 2))
    {
        sgmac_mode_cfg |= (1<<8);
    }

    if ((gb_datapath_master[lchip].sgmac_cfg[7].valid) &&
        (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac7 == 2))
    {
        sgmac_mode_cfg |= (1<<7);
    }

    if ((gb_datapath_master[lchip].sgmac_cfg[6].valid) &&
        (gb_datapath_master[lchip].hss_cfg.hss_used_sgmac6 == 2))
    {
        sgmac_mode_cfg |= (1<<6);
    }

    if ((gb_datapath_master[lchip].sgmac_cfg[5].valid) &&
        (gb_datapath_master[lchip].hss_cfg.sgmac5_is_xaui))
    {
        sgmac_mode_cfg |= (1<<1);
    }
    if ((gb_datapath_master[lchip].sgmac_cfg[4].valid) &&
        (gb_datapath_master[lchip].hss_cfg.sgmac4_is_xaui))
    {
        sgmac_mode_cfg |= 1;
    }

    /* SgmacModeCfg */
    DATAPATH_WRITE_CHIP(lchip, 0x000003c8, sgmac_mode_cfg);

    return DRV_E_NONE;
}

int32
drv_greatbelt_pll_qsgmii_init(uint8 chip_id)
{
    uint32 qsgmii_cfg = 0;
    uint32 index = 0;

    for (index = 0; index < 6; index++)
    {
        /* if not use qsgmii mode, set cfgQsgmiiDis  */
        if (0 == gb_datapath_master[chip_id].quadmac_cfg[index].valid)
        {
            qsgmii_cfg |= (1<<(8+index));

            /* sgmii mode need cofigure */
            qsgmii_cfg |= (1<<(16+index));
        }
    }

    /* cfg */
    if (gb_datapath_master[chip_id].hss_cfg.qsgmii9_sel_hss3)
    {
        qsgmii_cfg |= (1<<7);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii8_sel_hss3)
    {
        qsgmii_cfg |= (1<<6);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii7_sel_hss2)
    {
        qsgmii_cfg |= (1<<5);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii6_sel_hss2)
    {
        qsgmii_cfg |= (1<<4);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii3_sel_hss3)
    {
        qsgmii_cfg |= (1<<3);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii2_sel_hss3)
    {
        qsgmii_cfg |= (1<<2);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii1_sel_hss2)
    {
        qsgmii_cfg |= (1<<1);
    }

    if (gb_datapath_master[chip_id].hss_cfg.qsgmii0_sel_hss2)
    {
        qsgmii_cfg |= 1;
    }

    /* QsgmiiHssSelCfg */
    DATAPATH_WRITE_CHIP(chip_id, 0x000003c0, qsgmii_cfg);

    return DRV_E_NONE;
}

/**
@brief cfg net rx
   config register: NetRxMemInit, DsChannelizeMode
*/
int32
drv_greatbelt_datapath_cfg_netrx(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id, 0x00510020, 0x1111);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0xc0000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0xc0000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0xc0000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0xc0000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0x80000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0x80000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x512200, 0x80000);
    DATAPATH_WRITE_CHIP(chip_id, 0x512204, 0x25800021);
    DATAPATH_WRITE_CHIP(chip_id, 0x0051002c, 0);

    return DRV_E_NONE;
}

/**
@brief cfg IPE
   config register: IpeHdrAdjCfg, IpeIntfMapInitCtl, IpePktProcInitCtl, IpeFwdCfg
*/
int32
drv_greatbelt_datapath_cfg_ipe(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id, 0x801c80, 0x5 );
    DATAPATH_WRITE_CHIP(chip_id, 0x801c80, 0x1 );
    DATAPATH_WRITE_CHIP(chip_id, 0x817ed0, 0x1 );
    DATAPATH_WRITE_CHIP(chip_id, 0x836628, 0x1 );
    DATAPATH_WRITE_CHIP(chip_id, 0x841dbc, 0x87);

    return DRV_E_NONE;
}

/**
@brief cfg bufstore
*/
int32
drv_greatbelt_datapath_cfg_bufstore(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id,0xc15988,0x623000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15aa8,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xc15ac8,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xc15a90,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xc15ac0,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xc15a78,0x10011f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b04,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15708,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15708,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15708,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15708,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc1570c,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc1570c,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1570c,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1570c,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15710,0x1000101);
    DATAPATH_WRITE_CHIP(chip_id,0xc15710,0x101);
    DATAPATH_WRITE_CHIP(chip_id,0xc15710,0x100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15710,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15714,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15714,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15714,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15714,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15718,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15718,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15718,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15718,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc1571c,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc1571c,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1571c,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1571c,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15720,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15720,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15720,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15720,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15724,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15724,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15724,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15724,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15728,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15728,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15728,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15728,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc1572c,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc1572c,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1572c,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1572c,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15730,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15730,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15730,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15730,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15734,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15734,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15734,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15734,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15738,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15738,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15738,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15738,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc1573c,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc1573c,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1573c,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc1573c,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15740,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15740,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15740,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15740,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15744,0x1010100);
    DATAPATH_WRITE_CHIP(chip_id,0xc15744,0x1010000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15744,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xc15744,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b10,0xfff0f0f);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b18,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b18,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b18,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xc15b18,0x0);

    return DRV_E_NONE;
}

/**
@brief cfg metfifo
*/
int32
drv_greatbelt_datapath_cfg_metfifo(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id,0xc40038,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xc40480,0x1);

    return DRV_E_NONE;
}

/**
@brief cfg bufretrev
*/
int32
drv_greatbelt_datapath_cfg_bufretrev(uint8 chip_id)
{
    uint32 sgmac_chan_en_high = 0;
    uint32 sgmac_chan_en_low = 0;
    uint32 gmac_chan_en_high = 0;
    uint32 gmac_chan_en_low = 0;
    uint32 internel_chan = 0;
    uint8 valid = 0;
    uint8 chan_id = 0;
    uint8 lport = 0;
    uint32 cmd = 0;
    uint32 field_value = 0;

    for (lport = 0; lport < MAC_NUM; lport++)
    {
        if (gb_datapath_master[chip_id].port_capability[lport].valid)
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            if (DRV_PORT_TYPE_1G == gb_datapath_master[chip_id].port_capability[lport].port_type)
            {
                if (chan_id < 32)
                {
                    gmac_chan_en_low |= (1 << chan_id);
                }
                else
                {
                    gmac_chan_en_high |= (1 << (chan_id - 32));
                }
            }
            else
            {
                if (chan_id < 32)
                {
                    sgmac_chan_en_low |= (1 << chan_id);
                }
                else
                {
                    sgmac_chan_en_high |= (1 << (chan_id - 32));
                }
            }
        }
    }



    if ((sgmac_chan_en_low & gmac_chan_en_low) || (sgmac_chan_en_high & gmac_chan_en_high))
    {
        DRV_DBG_INFO("sgmac channel confilict with gmac channel, please check!!! \n");
        DRV_DBG_INFO("sgmac chan0~31 enable: 0x%x, chan32~63 enable:0x%x \n", sgmac_chan_en_low, sgmac_chan_en_high);
        DRV_DBG_INFO("gmac chan0~31 enable: 0x%x, chan32~63 enable:0x%x \n", gmac_chan_en_low, gmac_chan_en_high);
        return DRV_E_DATAPATH_CHANNEL_ENABLE_CONFLICT;
    }

    DATAPATH_WRITE_CHIP(chip_id,0xc87950,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xc87934,0x652830);

    /* cfg channel BufRetrvChanIdCfg_t */
    chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].chan_id;
    valid =  (INVALID_CHAN_ID != chan_id);
    internel_chan = ((valid<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc87800,internel_chan);

    /* init interlaken channel in IpeHdrAdjChanIdCfg */
    field_value = (valid == 1);
    cmd = DRV_IOW(IpeHdrAdjChanIdCfg_t, IpeHdrAdjChanIdCfg_CfgIntLkChanEnInt_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));
    if (field_value)
    {
        internel_chan = chan_id;
        cmd = DRV_IOW(IpeHdrAdjChanIdCfg_t, IpeHdrAdjChanIdCfg_CfgIntLkChanIdInt_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &internel_chan));
    }


    chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ILOOP_LPORT].chan_id;
    valid =  (INVALID_CHAN_ID != chan_id);
    internel_chan = ((valid<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc87804,internel_chan);

    /* init iloop channel in IpeHdrAdjChanIdCfg */
    field_value = (valid == 1);
    cmd = DRV_IOW(IpeHdrAdjChanIdCfg_t, IpeHdrAdjChanIdCfg_CfgILoopChanEnInt_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));
    if (field_value)
    {
        internel_chan = chan_id;
        cmd = DRV_IOW(IpeHdrAdjChanIdCfg_t, IpeHdrAdjChanIdCfg_CfgILoopChanIdInt_f);
        DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &internel_chan));
    }

    chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_CPU_LPORT].chan_id;
    valid =  (INVALID_CHAN_ID != chan_id);
    internel_chan = ((valid<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc87808,internel_chan);

    chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_DMA_LPORT].chan_id;
    valid =  (INVALID_CHAN_ID != chan_id);
    internel_chan = ((valid<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc8780c,internel_chan);

    chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_OAM_LPORT].chan_id;
    valid =  (INVALID_CHAN_ID != chan_id);
    internel_chan = ((valid<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc87810,internel_chan);

    chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].chan_id;
    valid =  (INVALID_CHAN_ID != chan_id);
    internel_chan = ((valid<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc87814,internel_chan);

    /* elog_channel is not valid*/
    chan_id = 0;
    internel_chan = ((0<<16) | chan_id);
    DATAPATH_WRITE_CHIP(chip_id,0xc87818,internel_chan);


    /* gmac channel id cfg */
    DATAPATH_WRITE_CHIP(chip_id,0xc8781c,gmac_chan_en_high);
    DATAPATH_WRITE_CHIP(chip_id,0xc87820,gmac_chan_en_low);

    /* sgmac channel id cfg */
    DATAPATH_WRITE_CHIP(chip_id,0xc87824,sgmac_chan_en_high);
    DATAPATH_WRITE_CHIP(chip_id,0xc87828,sgmac_chan_en_low);

    /* QMgrDrainCtl, 0x00dbad60*/
    DATAPATH_WRITE_CHIP(chip_id,0x00dbad60, (gmac_chan_en_high|sgmac_chan_en_high));
    DATAPATH_WRITE_CHIP(chip_id,0x00dbad64, (gmac_chan_en_low|sgmac_chan_en_low));

    return DRV_E_NONE;
}

/**
@brief cfg QMgr
*/
int32
drv_greatbelt_datapath_cfg_qmgr(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id,0xdbada0,0x10000);
    DATAPATH_WRITE_CHIP(chip_id,0xdbada0,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xdbad40,0x10000);
    DATAPATH_WRITE_CHIP(chip_id,0xdbad40,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xd0bd78,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xd0bb80,0x63210000);
    DATAPATH_WRITE_CHIP(chip_id,0xd0bb84,0x240);
    DATAPATH_WRITE_CHIP(chip_id,0xd0bb80,0x63210200);
    DATAPATH_WRITE_CHIP(chip_id,0xd0bd70,0x8);
    DATAPATH_WRITE_CHIP(chip_id,0xd0bd70,0x88);
    DATAPATH_WRITE_CHIP(chip_id,0xdbae00,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xdbae08,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xdbae10,0x80002fff);
    DATAPATH_WRITE_CHIP(chip_id,0xdbae18,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xdbada0,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xdbad40,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xdbad40,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xe40020,0x1);

    return DRV_E_NONE;
}

/**
@brief cfg epe
*/
int32
drv_greatbelt_datapath_cfg_epe(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id,0x8809b8,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x897350,0x111);
    DATAPATH_WRITE_CHIP(chip_id,0x8a147c,0x10);
    DATAPATH_WRITE_CHIP(chip_id,0x8c07a8,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x8e2f6c,0x1);

    return DRV_E_NONE;
}

/**
@brief cfg nettx
*/
int32
drv_greatbelt_datapath_cfg_nettx_sec1(uint8 chip_id)
{
    uint8 cal_num = 0;
    uint8 cal_num1 = 0;
    uint32 walk_end = 0;
    uint32 cmd = 0;
    net_tx_cal_ctl_t cal_ctl;

    /* cfg Calendar walker end pointer */
    cal_num = gb_datapath_master[chip_id].cal_infor[DRV_CALENDAR_PTR_ITEM_CTL].cal_walk_end;
    cal_num1 = gb_datapath_master[chip_id].cal_infor[DRV_CALENDAR_PTR_ITEM_BAK].cal_walk_end;
    walk_end |= (cal_num << 8);
    walk_end |= (cal_num1 << 16);

    DATAPATH_WRITE_CHIP(chip_id,0x600028,walk_end);
    DATAPATH_WRITE_CHIP(chip_id,0x600128,0x130e1710);

    /* cfg Calendar sel Ctl or Bak */
    cmd = DRV_IOR(NetTxCalCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &cal_ctl));
    cal_ctl.cal_entry_sel = gb_datapath_master[chip_id].calendar_sel;
    cmd = DRV_IOW(NetTxCalCtl_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &cal_ctl));

    /* cfg NetTxTxThrdCfg txThrd1 field to 0x5a */
    DATAPATH_WRITE_CHIP(chip_id,0x600030,0x14095a09);

    return DRV_E_NONE;
}

/**
@brief cfg Misc
*/
int32
drv_greatbelt_datapath_cfg_misc_sec1(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id,0x980038,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x3802254,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x3802258,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x1140208,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x1180c60,0x1000000);
    DATAPATH_WRITE_CHIP(chip_id,0xa21810,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x11e2230,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x11e2220,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x14000c4,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0x1198058,0x1);

    return DRV_E_NONE;
}

/**
@brief cfg Calendar
*/
int32
drv_greatbelt_datapath_cfg_calendar(uint8 chip_id)
{
    uint32 index = 0;
    uint8 chan_id = 0;
    uint8 mac_id = 0;
    uint8 lport = 0;
    uint32 mem_value = 0;
    uint16 start = 0;
    uint32 ptr_val = 0;
    uint16 gmac_step  = (gb_datapath_master[chip_id].nettxbuf_cfg.gmac_step/2);
    uint16 sgmac_step = (gb_datapath_master[chip_id].nettxbuf_cfg.sgmac_step/2);
    uint16 intlk_step = (gb_datapath_master[chip_id].nettxbuf_cfg.intlk_step/2);
    uint16 eloop_step = (gb_datapath_master[chip_id].nettxbuf_cfg.eloop_step/2);
    uint16 dxaui_step = (gb_datapath_master[chip_id].nettxbuf_cfg.dxaui_step/2);
    uint16 buffer_step = 0;
    uint16 buffer_step_internal[6] = {24, 12, 4, 4, 8, 12};
    uint16 weight_internal[6] = {50, 10,5,5,10,10};
    uint16 credic_internal[6] = {2,2,0,0,0,2};
    drv_datapath_sgmac_info_t sgmac_info;
    sal_memset(&sgmac_info, 0, sizeof(drv_datapath_sgmac_info_t));


    /* index is channel */
    start = 0;
    for (lport = 0; lport < MAC_NUM; lport++)
    {
        if (gb_datapath_master[chip_id].port_capability[lport].valid)
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            if (gb_datapath_master[chip_id].port_capability[lport].port_type == DRV_PORT_TYPE_1G)
            {
                buffer_step = 4;
            }
            else if (gb_datapath_master[chip_id].port_capability[lport].port_type == DRV_PORT_TYPE_SG)
            {
                buffer_step = 8;
            }

            /* BufRetrvBufConfigMem */
            ptr_val = DRV_DATAPATH_BIT_COMBINE(1, start, (start+buffer_step-1));
            DATAPATH_WRITE_CHIP(chip_id, 0xc87600+4*chan_id, ptr_val);

            /*BufRetrvBufStatusMem */
            ptr_val = DRV_DATAPATH_BIT_COMBINE(1, start, start);
            DATAPATH_WRITE_CHIP(chip_id, 0xc87300+4*chan_id, ptr_val);

            /* BufRetrvPktConfigMem */
            ptr_val = DRV_DATAPATH_BIT_COMBINE(1, start, (start+buffer_step-1));
            DATAPATH_WRITE_CHIP(chip_id, 0xc87200+4*chan_id, ptr_val);

            /* BufRetrvPktStatusMem */
            ptr_val = DRV_DATAPATH_BIT_COMBINE(1, start, start);
            DATAPATH_WRITE_CHIP(chip_id, 0xc87500+4*chan_id, ptr_val);

            start = start+buffer_step;
        }
    }

    for (lport = DRV_DATA_PATH_INTLK_LPORT; lport <= DRV_DATA_PATH_ELOOP_LPORT; lport++)
    {
        if ((gb_datapath_master[chip_id].port_capability[lport].valid))
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            if (chan_id != INVALID_CHAN_ID)
            {
                index = lport - DRV_DATA_PATH_INTLK_LPORT;
                /* BufRetrvBufConfigMem */
                ptr_val = DRV_DATAPATH_BIT_COMBINE(weight_internal[index], start, (start+buffer_step_internal[index]-1));
                DATAPATH_WRITE_CHIP(chip_id, 0xc87600+4*chan_id, ptr_val);

                /*BufRetrvBufStatusMem */
                ptr_val = DRV_DATAPATH_BIT_COMBINE(weight_internal[index], start, start);
                DATAPATH_WRITE_CHIP(chip_id, 0xc87300+4*chan_id, ptr_val);

                 /* BufRetrvPktConfigMem */
                ptr_val = DRV_DATAPATH_BIT_COMBINE(weight_internal[index], start, (start+buffer_step_internal[index]-1));
                DATAPATH_WRITE_CHIP(chip_id, 0xc87200+4*chan_id, ptr_val);

                /* BufRetrvPktStatusMem */
                ptr_val = DRV_DATAPATH_BIT_COMBINE(weight_internal[index], start, start);
                DATAPATH_WRITE_CHIP(chip_id, 0xc87500+4*chan_id, ptr_val);

                start = start+buffer_step_internal[index];
            }
        }
    }

    /* cfg BufRetrvBufCreditConfigMem for mac */
    for (lport = 0; lport < MAC_NUM; lport++)
    {
        /* get chan type */
        if ((gb_datapath_master[chip_id].port_capability[lport].valid))
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            if (gb_datapath_master[chip_id].port_capability[lport].port_type == DRV_PORT_TYPE_1G)
            {
                DATAPATH_WRITE_CHIP(chip_id,0xc87700+chan_id*4,0x0);
            }
            else if(gb_datapath_master[chip_id].port_capability[lport].port_type == DRV_PORT_TYPE_SG)
            {
                DATAPATH_WRITE_CHIP(chip_id,0xc87700+chan_id*4,0x1);
            }
        }
    }

    for (lport = DRV_DATA_PATH_ILOOP_LPORT; lport <= DRV_DATA_PATH_INTLK_LPORT; lport++)
    {
        if ((gb_datapath_master[chip_id].port_capability[lport].valid))
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            if (chan_id != INVALID_CHAN_ID)
            {
                index = lport - DRV_DATA_PATH_ILOOP_LPORT;
                /* BufRetrvBufCreditConfigMem */
                DATAPATH_WRITE_CHIP(chip_id, 0xc87700+4*chan_id, credic_internal[index]);
            }
        }
    }

    /* cfg BufRetrvCreditConfigMem for mac, index is channel */
    for (lport = 0; lport < MAC_NUM; lport++)
    {
        if (gb_datapath_master[chip_id].port_capability[lport].valid)
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            mem_value = (DRV_PORT_TYPE_SG == gb_datapath_master[chip_id].port_capability[lport].port_type);
            DATAPATH_WRITE_CHIP(chip_id, 0xc87000 + chan_id*4, mem_value);
        }
    }

    /* cfg BufRetrvCreditConfigMem for internal intlk and eloop, index is mac id */
    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id, 0xc87000 + chan_id*4, 0x2);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xc87000+chan_id*4,0x3);
    }


    /* cfg NetTxCalendarCtl 160 entry*/
    for (index = 0; index < 160; index++)
    {
        DATAPATH_WRITE_CHIP(chip_id, (0x601400 + 4*index),
            gb_datapath_master[chip_id].cal_infor[DRV_CALENDAR_PTR_ITEM_CTL].net_tx_cal_entry[index]);
    }

    /* cfg NetTxCalBak 160 entry */
    for (index = 0; index < 160; index++)
    {
        DATAPATH_WRITE_CHIP(chip_id, (0x604400 + 4*index),
            gb_datapath_master[chip_id].cal_infor[DRV_CALENDAR_PTR_ITEM_BAK].net_tx_cal_entry[index]);
    }


    start = 0;
    for (lport = 0; lport < MAC_NUM; lport++)
    {
        if (gb_datapath_master[chip_id].port_capability[lport].valid)
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            mac_id  = gb_datapath_master[chip_id].port_capability[lport].mac_id;
            /* cfg NetTxChStaticInfo/NetTxChDynamicInfo for gmac tx channel */
            if (DRV_PORT_TYPE_1G == gb_datapath_master[chip_id].port_capability[lport].port_type)
            {
                if (start + gmac_step > MAX_TX_POINTER_IDX)
                {
                    return DRV_E_DATAPATH_EXCEED_MAX_POINTER;
                }
                ptr_val = ((start & 0x7ff) << 12) | ((start + gmac_step -1)&0x7ff);
                DATAPATH_WRITE_CHIP(chip_id, 0x601000 + 4*mac_id, ptr_val);
                DATAPATH_WRITE_CHIP(chip_id, 0x601200 + 8*mac_id, 0x0);
                DATAPATH_WRITE_CHIP(chip_id, 0x601204 + 8*mac_id, ((start << 16) | start));
                start = start + gmac_step;

            } /*  cfg NetTxChStaticInfo/NetTxChDynamicInfo for sgmac tx channel */
            else if(DRV_PORT_TYPE_SG == gb_datapath_master[chip_id].port_capability[lport].port_type)
            {
                if (start + sgmac_step > MAX_TX_POINTER_IDX)
                {
                    return DRV_E_DATAPATH_EXCEED_MAX_POINTER;
                }

                if (sgmac_info.pcs_mode == DRV_SERDES_XAUI_MODE)
                {
                    sgmac_step = dxaui_step;
                }

                ptr_val = (1 << 24) | ((start & 0x7ff) << 12) | ((start + sgmac_step -1)&0x7ff);
                DATAPATH_WRITE_CHIP(chip_id, 0x601000 + 4*(mac_id), ptr_val);
                DATAPATH_WRITE_CHIP(chip_id, 0x601200 + 8*(mac_id), 0x0);
                DATAPATH_WRITE_CHIP(chip_id, 0x601204 + 8*(mac_id), ((start << 16) | start));
                start = start + sgmac_step;
            }
        }

    }

    /*nettx 60 interlaken used */
    if (drv_greatbelt_get_intlk_support(chip_id))
    {
        if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].valid)
        {
            if (start + intlk_step > MAX_TX_POINTER_IDX)
            {
                return DRV_E_DATAPATH_EXCEED_MAX_POINTER;
            }

            mac_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].mac_id;
            ptr_val = (1 << 24) | ((start & 0x7ff) << 12) | ((start + intlk_step - 1)&0x7ff);
            DATAPATH_WRITE_CHIP(chip_id, 0x601000 + 4*mac_id, ptr_val);

            DATAPATH_WRITE_CHIP(chip_id, 0x601200 + 8*mac_id, 0x0);
            DATAPATH_WRITE_CHIP(chip_id, 0x601204 + 8*mac_id, ((start << 16) | start));
            start = start + intlk_step + 1;
        }
    }

    /* eloop channel is used */
    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].valid)
    {
        /*get eloop port id to do*/
        mac_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].mac_id;
        if (start + eloop_step > MAX_TX_POINTER_IDX)
        {
            return DRV_E_DATAPATH_EXCEED_MAX_POINTER;
        }

        ptr_val = (2 << 24) | ((start & 0x7ff) << 12) | ((start + eloop_step - 1)&0x7ff);
        DATAPATH_WRITE_CHIP(chip_id, 0x601000 + 4*mac_id, ptr_val);

        DATAPATH_WRITE_CHIP(chip_id, 0x601200 + 8*mac_id, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0x601204 + 8*mac_id, ((start << 16) | start));
    }

    gb_datapath_master[chip_id].nettxbuf_cfg.cur_ptr = ptr_val;

    return DRV_E_NONE;
}

/**
@brief cfg channel map
*/
int32
drv_greatbelt_datapath_cfg_chan_map(uint8 chip_id)
{
     /*uint32 phy_port_map = 0;*/
    uint8  port_id = 0;
    uint32  mac_id = 0;
    uint32  chan_id = 0;
    uint8  lport = 0;
    uint8  eloop_channel = 0;
    uint8  interlaken_channel = 0;


    for (lport = 0; lport < MAC_NUM; lport++)
    {
        if (gb_datapath_master[chip_id].port_capability[lport].valid)
        {
            /* cfg NetTxChanIdMap, from channel id to mac id */
            chan_id = (uint32)gb_datapath_master[chip_id].port_capability[lport].chan_id;
            mac_id  = (uint32)gb_datapath_master[chip_id].port_capability[lport].mac_id;

            DATAPATH_WRITE_CHIP(chip_id, (0x00602000 + chan_id*4), mac_id);

            /* cfg NetTxPortIdMap, from mac id to channel id, gmac and sgmac */
            DATAPATH_WRITE_CHIP(chip_id, (0x00602100 + mac_id*4), chan_id);

            /* cfg NetRxChannelMap ,mac id -> channel id */
            DATAPATH_WRITE_CHIP(chip_id, (0x00512a00 + mac_id*4), chan_id);
        }
    }



    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].valid)
    {
        interlaken_channel = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].chan_id;
        port_id            = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].mac_id;

        /* cfg interlaken chan to port */
        DATAPATH_WRITE_CHIP(chip_id, (0x00602000 + interlaken_channel*4), (uint32)(port_id));
        /* cfg interlaken port to chan */
        DATAPATH_WRITE_CHIP(chip_id, (0x00602100 + (port_id)*4), (uint32)interlaken_channel);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].valid)
    {

        eloop_channel = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].chan_id;
        port_id       = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].mac_id;

         /* cfg eloop chan to port */
        DATAPATH_WRITE_CHIP(chip_id, (0x00602000 + eloop_channel*4), (uint32)(port_id));
        /* cfg eloop port to chan */
        DATAPATH_WRITE_CHIP(chip_id, (0x00602100 + (port_id)*4), (uint32)eloop_channel);
    }


    return DRV_E_NONE;
}

/**
@brief cfg net tx
*/
int32
drv_greatbelt_datapath_cfg_nettx_sec2(uint8 chip_id)
{
    uint32 index = 0;
    uint32 cmd = 0;
    net_tx_cfg_chan_id_t net_tx_cfg_chan_id;

    sal_memset(&net_tx_cfg_chan_id, 0, sizeof(net_tx_cfg_chan_id_t));
    /* cfg NetTxInit */
    DATAPATH_WRITE_CHIP(chip_id,0x600020,0x1);

    /* cfg NetTxEpeStallCtl */
    DATAPATH_WRITE_CHIP(chip_id,0x600038,0x10);

    /* cfg NetTxEeeTimerTab */
    for (index = 0; index < 60; index++)
    {
        DATAPATH_WRITE_CHIP(chip_id, (0x603200+index*4), 0x0);
    }

    /* cfg DsChannelizeMode */
    DATAPATH_WRITE_CHIP(chip_id,0x512200,0x80000);
    DATAPATH_WRITE_CHIP(chip_id,0x512204,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512208,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51220c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512210,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512214,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512218,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51221c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512220,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512224,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512228,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51222c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512230,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512234,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512238,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51223c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512240,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512244,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512248,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51224c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512250,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512254,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512258,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51225c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512260,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512264,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512268,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51226c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512270,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512274,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512278,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51227c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512280,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512284,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512288,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51228c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512290,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512294,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512298,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51229c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122a0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122a4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122a8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122ac,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122b0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122b4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122b8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122bc,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122c0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122c4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122c8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122cc,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122d0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122d4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122d8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122dc,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122e0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122e4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122e8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122ec,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122f0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122f4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122f8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5122fc,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512300,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512304,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512308,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51230c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512310,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512314,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512318,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51231c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512320,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512324,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512328,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51232c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512330,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512334,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512338,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51233c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512340,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512344,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512348,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51234c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512350,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512354,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512358,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51235c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512360,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512364,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512368,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51236c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512370,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512374,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512378,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51237c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512380,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512384,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512388,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51238c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512390,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x512394,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x512398,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x51239c,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123a0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123a4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123a8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123ac,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123b0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123b4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123b8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123bc,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123c0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123c4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123c8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123cc,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123d0,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123d4,0x25800000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123d8,0x1c0000);
    DATAPATH_WRITE_CHIP(chip_id,0x5123dc,0x25800000);

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].valid)
    {
        if (drv_greatbelt_get_intlk_support(chip_id))
        {
            net_tx_cfg_chan_id.cfg_int_lk_chan_en_int = 1;
            net_tx_cfg_chan_id.cfg_int_lk_chan_id_int = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].chan_id;
        }
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].valid)
    {
        net_tx_cfg_chan_id.cfg_e_loop_chan_en_int = 1;
        net_tx_cfg_chan_id.cfg_e_loop_chan_id_int = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].chan_id;
    }
    cmd = DRV_IOW(NetTxCfgChanId_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &net_tx_cfg_chan_id));


    return DRV_E_NONE;
}

/**
@brief cfg Misc
*/
int32
drv_greatbelt_datapath_cfg_misc_sec2(uint8 chip_id)
{
    DATAPATH_WRITE_CHIP(chip_id,0xc15a7c,0x24000000);
    DATAPATH_WRITE_CHIP(chip_id,0x880994,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0x600060,0x1110000);
    DATAPATH_WRITE_CHIP(chip_id,0x600060,0x110000);
    DATAPATH_WRITE_CHIP(chip_id,0x600060,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0x1180c64,0x1fff);
    DATAPATH_WRITE_CHIP(chip_id,0x1180c60,0x11000000);

    return DRV_E_NONE;
}

/**
@brief cfg Queue
*/
int32
drv_greatbelt_datapath_cfg_queue(uint8 chip_id)
{
    uint32 index = 0;
    uint8 lport = 0;
    uint8 chan_id = 0;

    /* cfg DsChanCredit and QMgrNetworkWtCfgMem */
    for (lport = 0; lport < MAC_NUM; lport++)
    {
        if (gb_datapath_master[chip_id].port_capability[lport].valid)
        {
            chan_id = gb_datapath_master[chip_id].port_capability[lport].chan_id;
            if (gb_datapath_master[chip_id].port_capability[lport].port_type == DRV_PORT_TYPE_1G)
            {
                DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4,0x4);
                DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,0x1);
            }
            else if (gb_datapath_master[chip_id].port_capability[lport].port_type == DRV_PORT_TYPE_SG)
            {
                DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4,0x8);
                DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,10);
            }
        }
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_INTLK_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4, 24);
        DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,50);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ILOOP_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ILOOP_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4, 12);
        DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,25);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_CPU_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_CPU_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4, 4);
        DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,5);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_DMA_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_DMA_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4, 4);
        DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,5);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_OAM_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_OAM_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4, 8);
        DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,10);
    }

    if (gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].valid)
    {
        chan_id = gb_datapath_master[chip_id].port_capability[DRV_DATA_PATH_ELOOP_LPORT].chan_id;
        DATAPATH_WRITE_CHIP(chip_id,0xdba900+chan_id*4, 11);
        DATAPATH_WRITE_CHIP(chip_id,0xdba600+chan_id*4,25);
    }


    /* cfg DsQueShpWfqCtl */
    for (index = 0; index < 1024; index++)
    {
        DATAPATH_WRITE_CHIP(chip_id, 0xda8000+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8004+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8008+index*0x10, 0x0);

        DATAPATH_WRITE_CHIP(chip_id, 0xda8000+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8004+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8008+index*0x10, 0x0);

        DATAPATH_WRITE_CHIP(chip_id, 0xda8000+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8004+index*0x10, 0x200);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8008+index*0x10, 0x0);

        DATAPATH_WRITE_CHIP(chip_id, 0xda8000+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8004+index*0x10, 0xa0200);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8008+index*0x10, 0x0);

        DATAPATH_WRITE_CHIP(chip_id, 0xda8000+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8004+index*0x10, 0xa0200);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8008+index*0x10, 0x200);

        DATAPATH_WRITE_CHIP(chip_id, 0xda8000+index*0x10, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8004+index*0x10, 0xa0200);
        DATAPATH_WRITE_CHIP(chip_id, 0xda8008+index*0x10, 0xa0200);
    }

    /* cfg DsGrpShpWfqCtl */
    for (index = 0; index < 256; index++)
    {
        DATAPATH_WRITE_CHIP(chip_id, 0xda0000+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0004+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0008+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda000c+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0010+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0014+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0018+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda001c+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0020+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0024+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0028+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda002c+index*0x40, 0xff0000);

        DATAPATH_WRITE_CHIP(chip_id, 0xda0000+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0004+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0008+index*0x40, 0x800000);
        DATAPATH_WRITE_CHIP(chip_id, 0xda000c+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0010+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0014+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0018+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda001c+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0020+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0024+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0028+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda002c+index*0x40, 0xff0000);

        DATAPATH_WRITE_CHIP(chip_id, 0xda0000+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0004+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0008+index*0x40, 0x80000a);
        DATAPATH_WRITE_CHIP(chip_id, 0xda000c+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0010+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0014+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0018+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda001c+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0020+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0024+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda0028+index*0x40, 0x0);
        DATAPATH_WRITE_CHIP(chip_id, 0xda002c+index*0x40, 0xff0000);
    }


    /* cfg DsHeadHashMod */
    DATAPATH_WRITE_CHIP(chip_id,0xd09c00,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c04,0x1111);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c08,0x2222);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c0c,0x333);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c10,0x1444);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c14,0x2055);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c18,0x106);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c1c,0x1210);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c20,0x2321);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c24,0x432);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c28,0x1043);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c2c,0x2154);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c30,0x205);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c34,0x1316);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c38,0x2420);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c3c,0x31);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c40,0x1142);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c44,0x2253);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c48,0x304);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c4c,0x1415);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c50,0x2026);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c54,0x130);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c58,0x1241);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c5c,0x2352);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c60,0x403);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c64,0x1014);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c68,0x2125);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c6c,0x236);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c70,0x1340);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c74,0x2451);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c78,0x2);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c7c,0x1113);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c80,0x2224);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c84,0x335);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c88,0x1446);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c8c,0x2050);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c90,0x101);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c94,0x1212);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c98,0x2323);
    DATAPATH_WRITE_CHIP(chip_id,0xd09c9c,0x434);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ca0,0x1045);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ca4,0x2156);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ca8,0x200);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cac,0x1311);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cb0,0x2422);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cb4,0x33);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cb8,0x1144);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cbc,0x2255);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cc0,0x306);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cc4,0x1410);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cc8,0x2021);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ccc,0x132);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cd0,0x1243);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cd4,0x2354);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cd8,0x405);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cdc,0x1016);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ce0,0x2120);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ce4,0x231);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ce8,0x1342);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cec,0x2453);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cf0,0x4);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cf4,0x1115);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cf8,0x2226);
    DATAPATH_WRITE_CHIP(chip_id,0xd09cfc,0x330);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d00,0x1441);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d04,0x2052);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d08,0x103);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d0c,0x1214);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d10,0x2325);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d14,0x436);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d18,0x1040);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d1c,0x2151);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d20,0x202);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d24,0x1313);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d28,0x2424);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d2c,0x35);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d30,0x1146);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d34,0x2250);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d38,0x301);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d3c,0x1412);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d40,0x2023);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d44,0x134);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d48,0x1245);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d4c,0x2356);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d50,0x400);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d54,0x1011);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d58,0x2122);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d5c,0x233);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d60,0x1344);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d64,0x2455);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d68,0x6);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d6c,0x1110);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d70,0x2221);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d74,0x332);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d78,0x1443);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d7c,0x2054);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d80,0x105);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d84,0x1216);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d88,0x2320);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d8c,0x431);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d90,0x1042);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d94,0x2153);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d98,0x204);
    DATAPATH_WRITE_CHIP(chip_id,0xd09d9c,0x1315);
    DATAPATH_WRITE_CHIP(chip_id,0xd09da0,0x2426);
    DATAPATH_WRITE_CHIP(chip_id,0xd09da4,0x30);
    DATAPATH_WRITE_CHIP(chip_id,0xd09da8,0x1141);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dac,0x2252);
    DATAPATH_WRITE_CHIP(chip_id,0xd09db0,0x303);
    DATAPATH_WRITE_CHIP(chip_id,0xd09db4,0x1414);
    DATAPATH_WRITE_CHIP(chip_id,0xd09db8,0x2025);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dbc,0x136);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dc0,0x1240);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dc4,0x2351);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dc8,0x402);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dcc,0x1013);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dd0,0x2124);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dd4,0x235);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dd8,0x1346);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ddc,0x2450);
    DATAPATH_WRITE_CHIP(chip_id,0xd09de0,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xd09de4,0x1112);
    DATAPATH_WRITE_CHIP(chip_id,0xd09de8,0x2223);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dec,0x334);
    DATAPATH_WRITE_CHIP(chip_id,0xd09df0,0x1445);
    DATAPATH_WRITE_CHIP(chip_id,0xd09df4,0x2056);
    DATAPATH_WRITE_CHIP(chip_id,0xd09df8,0x100);
    DATAPATH_WRITE_CHIP(chip_id,0xd09dfc,0x1211);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e00,0x2322);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e04,0x433);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e08,0x1044);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e0c,0x2155);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e10,0x206);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e14,0x1310);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e18,0x2421);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e1c,0x32);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e20,0x1143);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e24,0x2254);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e28,0x305);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e2c,0x1416);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e30,0x2020);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e34,0x131);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e38,0x1242);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e3c,0x2353);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e40,0x404);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e44,0x1015);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e48,0x2126);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e4c,0x230);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e50,0x1341);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e54,0x2452);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e58,0x3);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e5c,0x1114);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e60,0x2225);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e64,0x336);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e68,0x1440);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e6c,0x2051);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e70,0x102);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e74,0x1213);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e78,0x2324);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e7c,0x435);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e80,0x1046);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e84,0x2150);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e88,0x201);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e8c,0x1312);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e90,0x2423);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e94,0x34);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e98,0x1145);
    DATAPATH_WRITE_CHIP(chip_id,0xd09e9c,0x2256);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ea0,0x300);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ea4,0x1411);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ea8,0x2022);
    DATAPATH_WRITE_CHIP(chip_id,0xd09eac,0x133);
    DATAPATH_WRITE_CHIP(chip_id,0xd09eb0,0x1244);
    DATAPATH_WRITE_CHIP(chip_id,0xd09eb4,0x2355);
    DATAPATH_WRITE_CHIP(chip_id,0xd09eb8,0x406);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ebc,0x1010);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ec0,0x2121);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ec4,0x232);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ec8,0x1343);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ecc,0x2454);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ed0,0x5);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ed4,0x1116);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ed8,0x2220);
    DATAPATH_WRITE_CHIP(chip_id,0xd09edc,0x331);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ee0,0x1442);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ee4,0x2053);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ee8,0x104);
    DATAPATH_WRITE_CHIP(chip_id,0xd09eec,0x1215);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ef0,0x2326);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ef4,0x430);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ef8,0x1041);
    DATAPATH_WRITE_CHIP(chip_id,0xd09efc,0x2152);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f00,0x203);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f04,0x1314);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f08,0x2425);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f0c,0x36);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f10,0x1140);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f14,0x2251);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f18,0x302);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f1c,0x1413);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f20,0x2024);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f24,0x135);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f28,0x1246);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f2c,0x2350);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f30,0x401);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f34,0x1012);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f38,0x2123);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f3c,0x234);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f40,0x1345);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f44,0x2456);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f48,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f4c,0x1111);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f50,0x2222);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f54,0x333);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f58,0x1444);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f5c,0x2055);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f60,0x106);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f64,0x1210);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f68,0x2321);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f6c,0x432);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f70,0x1043);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f74,0x2154);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f78,0x205);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f7c,0x1316);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f80,0x2420);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f84,0x31);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f88,0x1142);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f8c,0x2253);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f90,0x304);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f94,0x1415);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f98,0x2026);
    DATAPATH_WRITE_CHIP(chip_id,0xd09f9c,0x130);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fa0,0x1241);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fa4,0x2352);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fa8,0x403);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fac,0x1014);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fb0,0x2125);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fb4,0x236);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fb8,0x1340);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fbc,0x2451);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fc0,0x2);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fc4,0x1113);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fc8,0x2224);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fcc,0x335);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fd0,0x1446);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fd4,0x2050);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fd8,0x101);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fdc,0x1212);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fe0,0x2323);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fe4,0x434);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fe8,0x1045);
    DATAPATH_WRITE_CHIP(chip_id,0xd09fec,0x2156);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ff0,0x200);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ff4,0x1311);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ff8,0x2422);
    DATAPATH_WRITE_CHIP(chip_id,0xd09ffc,0x33);

    /* cfg DsSgmacHeadHashMod */
    DATAPATH_WRITE_CHIP(chip_id,0xd0a400,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a404,0x1111);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a408,0x2222);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a40c,0x333);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a410,0x1444);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a414,0x2055);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a418,0x106);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a41c,0x1210);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a420,0x2321);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a424,0x432);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a428,0x1043);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a42c,0x2154);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a430,0x205);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a434,0x1316);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a438,0x2420);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a43c,0x31);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a440,0x1142);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a444,0x2253);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a448,0x304);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a44c,0x1415);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a450,0x2026);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a454,0x130);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a458,0x1241);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a45c,0x2352);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a460,0x403);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a464,0x1014);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a468,0x2125);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a46c,0x236);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a470,0x1340);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a474,0x2451);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a478,0x2);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a47c,0x1113);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a480,0x2224);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a484,0x335);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a488,0x1446);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a48c,0x2050);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a490,0x101);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a494,0x1212);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a498,0x2323);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a49c,0x434);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4a0,0x1045);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4a4,0x2156);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4a8,0x200);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4ac,0x1311);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4b0,0x2422);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4b4,0x33);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4b8,0x1144);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4bc,0x2255);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4c0,0x306);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4c4,0x1410);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4c8,0x2021);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4cc,0x132);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4d0,0x1243);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4d4,0x2354);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4d8,0x405);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4dc,0x1016);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4e0,0x2120);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4e4,0x231);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4e8,0x1342);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4ec,0x2453);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4f0,0x4);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4f4,0x1115);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4f8,0x2226);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a4fc,0x330);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a500,0x1441);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a504,0x2052);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a508,0x103);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a50c,0x1214);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a510,0x2325);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a514,0x436);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a518,0x1040);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a51c,0x2151);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a520,0x202);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a524,0x1313);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a528,0x2424);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a52c,0x35);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a530,0x1146);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a534,0x2250);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a538,0x301);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a53c,0x1412);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a540,0x2023);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a544,0x134);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a548,0x1245);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a54c,0x2356);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a550,0x400);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a554,0x1011);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a558,0x2122);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a55c,0x233);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a560,0x1344);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a564,0x2455);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a568,0x6);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a56c,0x1110);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a570,0x2221);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a574,0x332);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a578,0x1443);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a57c,0x2054);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a580,0x105);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a584,0x1216);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a588,0x2320);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a58c,0x431);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a590,0x1042);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a594,0x2153);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a598,0x204);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a59c,0x1315);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5a0,0x2426);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5a4,0x30);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5a8,0x1141);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5ac,0x2252);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5b0,0x303);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5b4,0x1414);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5b8,0x2025);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5bc,0x136);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5c0,0x1240);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5c4,0x2351);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5c8,0x402);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5cc,0x1013);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5d0,0x2124);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5d4,0x235);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5d8,0x1346);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5dc,0x2450);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5e0,0x1);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5e4,0x1112);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5e8,0x2223);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5ec,0x334);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5f0,0x1445);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5f4,0x2056);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5f8,0x100);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a5fc,0x1211);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a600,0x2322);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a604,0x433);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a608,0x1044);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a60c,0x2155);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a610,0x206);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a614,0x1310);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a618,0x2421);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a61c,0x32);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a620,0x1143);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a624,0x2254);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a628,0x305);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a62c,0x1416);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a630,0x2020);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a634,0x131);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a638,0x1242);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a63c,0x2353);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a640,0x404);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a644,0x1015);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a648,0x2126);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a64c,0x230);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a650,0x1341);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a654,0x2452);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a658,0x3);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a65c,0x1114);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a660,0x2225);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a664,0x336);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a668,0x1440);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a66c,0x2051);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a670,0x102);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a674,0x1213);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a678,0x2324);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a67c,0x435);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a680,0x1046);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a684,0x2150);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a688,0x201);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a68c,0x1312);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a690,0x2423);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a694,0x34);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a698,0x1145);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a69c,0x2256);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6a0,0x300);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6a4,0x1411);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6a8,0x2022);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6ac,0x133);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6b0,0x1244);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6b4,0x2355);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6b8,0x406);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6bc,0x1010);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6c0,0x2121);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6c4,0x232);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6c8,0x1343);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6cc,0x2454);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6d0,0x5);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6d4,0x1116);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6d8,0x2220);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6dc,0x331);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6e0,0x1442);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6e4,0x2053);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6e8,0x104);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6ec,0x1215);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6f0,0x2326);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6f4,0x430);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6f8,0x1041);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a6fc,0x2152);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a700,0x203);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a704,0x1314);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a708,0x2425);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a70c,0x36);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a710,0x1140);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a714,0x2251);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a718,0x302);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a71c,0x1413);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a720,0x2024);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a724,0x135);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a728,0x1246);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a72c,0x2350);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a730,0x401);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a734,0x1012);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a738,0x2123);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a73c,0x234);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a740,0x1345);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a744,0x2456);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a748,0x0);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a74c,0x1111);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a750,0x2222);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a754,0x333);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a758,0x1444);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a75c,0x2055);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a760,0x106);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a764,0x1210);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a768,0x2321);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a76c,0x432);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a770,0x1043);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a774,0x2154);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a778,0x205);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a77c,0x1316);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a780,0x2420);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a784,0x31);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a788,0x1142);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a78c,0x2253);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a790,0x304);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a794,0x1415);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a798,0x2026);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a79c,0x130);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7a0,0x1241);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7a4,0x2352);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7a8,0x403);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7ac,0x1014);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7b0,0x2125);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7b4,0x236);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7b8,0x1340);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7bc,0x2451);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7c0,0x2);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7c4,0x1113);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7c8,0x2224);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7cc,0x335);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7d0,0x1446);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7d4,0x2050);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7d8,0x101);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7dc,0x1212);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7e0,0x2323);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7e4,0x434);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7e8,0x1045);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7ec,0x2156);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7f0,0x200);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7f4,0x1311);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7f8,0x2422);
    DATAPATH_WRITE_CHIP(chip_id,0xd0a7fc,0x33);

    /* cfg NetTxInit */
    DATAPATH_WRITE_CHIP(chip_id,0x600020,0x11);

    /* cfg NetTxPtpEnCtl */
    DATAPATH_WRITE_CHIP(chip_id,0x60006c,0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id,0x600068,0xfffffff);

    /* cfg IpeMuxHeaderAdjustCtl */
    DATAPATH_WRITE_CHIP(chip_id,0x801c14,0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id,0x801c10,0xffffffff);

    /* cfg MetFifoWrrCfg */
    DATAPATH_WRITE_CHIP(chip_id,0xc40060,0xf041041);

    return DRV_E_NONE;
}

/**
@brief cfg eco
*/
int32
drv_greatbelt_datapath_cfg_eco(uint8 chip_id)
{
    /* cfg BufRetrvPktWeightConfig */
    DATAPATH_WRITE_CHIP(chip_id,0xc87930,0x28582805);
    DATAPATH_WRITE_CHIP(chip_id,0xc87930,0x28580505);
    DATAPATH_WRITE_CHIP(chip_id,0xc87930,0x280a0505);

    /* cfg BufRetrvBufWeightConfig */
    DATAPATH_WRITE_CHIP(chip_id,0xc87934,0x652805);
    DATAPATH_WRITE_CHIP(chip_id,0xc87934,0x650505);

    return DRV_E_NONE;
}

int32
drv_greatbelt_datapath_cfg_bufretrv(uint8 chip_id)
{
    uint32 cmd = 0;

    /* 0x00c87920 */
    buf_retrv_credit_config_t buf_retrv_credit_config;
    sal_memset(&buf_retrv_credit_config, 0, sizeof(buf_retrv_credit_config_t));

    buf_retrv_credit_config.credit_config0 = (gb_datapath_master[chip_id].nettxbuf_cfg.gmac_step - 20);
    /* sgmac config shares with DXAUI */
    buf_retrv_credit_config.credit_config1 = (gb_datapath_master[chip_id].nettxbuf_cfg.sgmac_step - 20);
    buf_retrv_credit_config.credit_config2 = (gb_datapath_master[chip_id].nettxbuf_cfg.intlk_step - 20);
    buf_retrv_credit_config.credit_config3 = (gb_datapath_master[chip_id].nettxbuf_cfg.eloop_step - 20);

    cmd = DRV_IOW(BufRetrvCreditConfig_t, DRV_ENTRY_FLAG);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &buf_retrv_credit_config));

    return DRV_E_NONE;
}

int32
drv_greatbelt_init_datapath(uint8 chip_id)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 index = 0;
    uint32 temp =0;


    /* set sgmii autonegotiate */
    field_value = 1;
    cmd = DRV_IOW(LinkTimerCtl_t, LinkTimerCtl_LinkTimerEn_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));

    /* release all sub-module soft-reset */
    DATAPATH_WRITE_CHIP(chip_id, 0x20, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x24, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x28, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x2c, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x34, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x38, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x3c, 0xffffffff);

    for(index = 0; index < 32; index++)
    {
        DATAPATH_READ_CHIP(chip_id, (0x20), &temp);
        temp &= ~(1<<index);
        DATAPATH_WRITE_CHIP(chip_id, (0x20), temp);
    }

    for(index = 0; index < 32; index++)
    {
        DATAPATH_READ_CHIP(chip_id, (0x24), &temp);
        temp &= ~(1<<index);
        DATAPATH_WRITE_CHIP(chip_id, (0x24), temp);
    }

    /* release all sub-module soft-reset */
    DATAPATH_WRITE_CHIP(chip_id, 0x20, 0xffffffff);
    DATAPATH_WRITE_CHIP(chip_id, 0x24, 0xffffffff);

    for(index = 0; index < 32; index++)
    {
        /* resetMdio(15)will be done in _sys_greatbelt_peri_init
         * resetLed(14) will be done in drv_greatbelt_init_led
         * resetPtpEngine(13) will be done in sys_greatbelt_ptp_init
         */
        if ((15 == index) || (14 == index) || (13 == index))
        {
            continue;
        }
        DATAPATH_READ_CHIP(chip_id, (0x20), &temp);
        temp &= ~(1<<index);
        DATAPATH_WRITE_CHIP(chip_id, (0x20), temp);
    }

    for(index = 0; index < 32; index++)
    {
        DATAPATH_READ_CHIP(chip_id, (0x24), &temp);
        temp &= ~(1<<index);
        DATAPATH_WRITE_CHIP(chip_id, (0x24), temp);
    }

    cmd = DRV_IOR(PcieTraceCtl_t, PcieTraceCtl_TraceState_f);
    (DRV_IOCTL(chip_id, 0, cmd, &temp));
    DRV_DATAPATH_DBG_OUT("the traceState is 0x%x\n", temp);

    /* configure NetRx */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_netrx(chip_id));
    /* configure IPE */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_ipe(chip_id));
    /* configure BufStore */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_bufstore(chip_id));
    /* configure MetFifo */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_metfifo(chip_id));
    /*configure PbCtl */
    DATAPATH_WRITE_CHIP(chip_id,0x2000050,0x1);
    /* configure BufRetrev*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_bufretrev(chip_id));
    /* configure QMgr*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_qmgr(chip_id));
    /* configure EPE*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_epe(chip_id));
    /* configure NetTxMisc*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_nettx_sec1(chip_id));
    /* configure Misc*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_misc_sec1(chip_id));
    /* configure Calendar*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_calendar(chip_id));


    /* configure Net Tx */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_nettx_sec2(chip_id));

    /* configure ChannelMap*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_chan_map(chip_id));

    /* config Misc */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_misc_sec2(chip_id));

    /* queue */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_queue(chip_id));

    /* configure ECO*/
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_eco(chip_id));

    /* init buffer retrive credit config */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_cfg_bufretrv(chip_id));

    cmd = DRV_IOR(DeviceId_t, DeviceId_DeviceId_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value ));
    if ( 0x25 == field_value )
    {
        DATAPATH_WRITE_CHIP(chip_id,0x2e18,0x1);
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_get_datapath_info(uint8 chip_id)
{
    uint8 index = 0;
    uint8 lchip = chip_id;
    uint8 temp = 0;
    uint8 macro_idx = 0;
    uint8 pcs_mode = 0;
    uint8 sgmii_index = 0;
    uint8 sgmii_flag = 0;
    uint8 xsgmii_flag = 0;
    uint8 qsgmii_flag = 0;
    uint8 xfi_flag = 0;
    uint8 first_na = 0;
    uint8 lane_serdes_mapping[HSS_SERDES_LANE_NUM] =
    {
       0, 1, 2, 3, 4, 5, 6, 7,             /* hss0, lane_idx seq */
       0, 1, 2, 3, 4, 5, 6, 7,             /* hss1, lane_idx seq */
       4, 0, 5, 1, 6, 2, 7, 3,             /* hss2, lane_idx seq */
       4, 0, 5, 1, 6, 2, 7, 3              /* hss3, lane_idx seq */
    };

    for (macro_idx = 0; macro_idx < HSS_MACRO_NUM; macro_idx++)
    {
        /* get auxsel information */
        if (gb_datapath_master[lchip].pll_cfg.hss_pll[macro_idx].hss_pll_cfg1 & 0x80)
        {
            /* use auxsel */
            gb_datapath_master[lchip].pll_cfg.hss_pll[macro_idx].aux_sel = 1;
        }
        else
        {
            /* not use auxsel */
            gb_datapath_master[lchip].pll_cfg.hss_pll[macro_idx].aux_sel = 0;
        }

       sgmii_flag = 0;
       xsgmii_flag = 0;
       qsgmii_flag = 0;
       xfi_flag = 0;

       for (index = macro_idx*8; index < macro_idx*8+8; index++)
       {
           /*init flag*/
           if ((index%4) == 0)
           {
               first_na = 0;
           }

           temp = gb_datapath_master[lchip].serdes_cfg[index].mode;

           if (DRV_SERDES_NONE_MODE == temp)
           {
               if ((index%4) == 0)
               {
                  first_na = 1;
               }
               continue;
           }

           pcs_mode = temp;

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

           if (!xfi_flag)
           {
               xfi_flag = (DRV_SERDES_XFI_MODE == pcs_mode) ? 1 : 0;
           }

            /*special process for first NA serdes, refer to bug 30862*/
            if ((first_na) && (pcs_mode == DRV_SERDES_SGMII_MODE))
            {
                gb_datapath_master[lchip].serdes_cfg[(index/4)*4].mode = DRV_SERDES_NA_SGMII_MODE;
                gb_datapath_master[lchip].serdes_cfg[(index/4)*4].speed = DRV_SERDES_SPPED_1DOT25G;
            }
       }

       if (sgmii_flag || xsgmii_flag)
       {
           if (qsgmii_flag && xfi_flag)
           {
               DRV_DBG_INFO("This mix mode is not support! \n");
               return DRV_E_INVALID_PARAMETER;
           }

           if (qsgmii_flag)
           {
               pcs_mode = DRV_SERDES_MIX_MODE_WITH_QSGMII;
           }
           else if (xfi_flag)
           {
               pcs_mode = DRV_SERDES_MIX_MODE_WITH_XFI;
           }
       }

       gb_datapath_master[lchip].hss_cfg.hss_mode[macro_idx] = pcs_mode;

    }

    /* init serdes and lane relationship */
    for (index = 0; index < HSS_SERDES_LANE_NUM; index++)
    {
        gb_datapath_master[lchip].serdes_cfg[index].hss_id = index/8;
        gb_datapath_master[lchip].serdes_cfg[index].lane_idx = lane_serdes_mapping[index];
    }

    /* for sgmii48~59 connect to sgmac0~11, pcs mode is XGSGMII */
    for (index = 0; index < HSS_SERDES_LANE_NUM; index++)
    {
        if (gb_datapath_master[lchip].serdes_cfg[index].mode == DRV_SERDES_SGMII_MODE)
        {
             sgmii_index = gb_datapath_master[lchip].serdes_cfg[index].mac_id;
             if ((sgmii_index >= 48) && (sgmii_index <= 59))
             {
                 gb_datapath_master[lchip].serdes_cfg[index].mode = DRV_SERDES_XGSGMII_MODE;
                 gb_datapath_master[lchip].serdes_cfg[index].mac_id = sgmii_index;
             }
        }
    }

    /* init mac soft table */
    drv_greatbelt_datapath_init_mac_softtb(lchip);

    /* init internal channel */
    drv_greatbelt_datapath_init_internal_chan(lchip);

   /* get sgmac reference hss infor */
   if (gb_datapath_master[lchip].sgmac_cfg[6].valid)
    {
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac6 = gb_datapath_master[lchip].sgmac_cfg[6].serdes_id/8;
    }

   if (gb_datapath_master[lchip].sgmac_cfg[7].valid)
    {
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac7 = gb_datapath_master[lchip].sgmac_cfg[7].serdes_id/8;
    }

    if (gb_datapath_master[lchip].sgmac_cfg[8].valid)
    {
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac8 = gb_datapath_master[lchip].sgmac_cfg[8].serdes_id/8;
    }

     if (gb_datapath_master[lchip].sgmac_cfg[9].valid)
    {
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac9 = gb_datapath_master[lchip].sgmac_cfg[9].serdes_id/8;
    }

    if (gb_datapath_master[lchip].sgmac_cfg[10].valid)
    {
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac10 = gb_datapath_master[lchip].sgmac_cfg[10].serdes_id/8;
    }

    if (gb_datapath_master[lchip].sgmac_cfg[11].valid)
    {
        gb_datapath_master[lchip].hss_cfg.hss_used_sgmac11 = gb_datapath_master[lchip].sgmac_cfg[11].serdes_id/8;
    }

    if (gb_datapath_master[lchip].sgmac_cfg[4].valid)
    {
        if(gb_datapath_master[lchip].sgmac_cfg[4].pcs_mode == DRV_SERDES_XAUI_MODE)
        {
            gb_datapath_master[lchip].hss_cfg.sgmac4_is_xaui = 1;
        }
    }

    if (gb_datapath_master[lchip].sgmac_cfg[5].valid)
    {
        if(gb_datapath_master[lchip].sgmac_cfg[5].pcs_mode == DRV_SERDES_XAUI_MODE)
        {
            gb_datapath_master[lchip].hss_cfg.sgmac5_is_xaui = 1;
        }
    }

    /* qsgmii infor */
    if (gb_datapath_master[lchip].quadmac_cfg[9].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[9].serdes_id;
        if (temp >= 24)
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii9_sel_hss3 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[8].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[8].serdes_id;
        if (temp >= 24)
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii8_sel_hss3 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[7].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[7].serdes_id;
        if ((temp >= 16) && (temp < 24))
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii7_sel_hss2 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[6].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[6].serdes_id;
        if ((temp >= 16) && (temp < 24))
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii6_sel_hss2 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[3].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[3].serdes_id;
        if (temp >= 24)
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii3_sel_hss3 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[2].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[2].serdes_id;
        if (temp >= 24)
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii2_sel_hss3 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[1].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[1].serdes_id;
        if ((temp >= 16) && (temp < 24))
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii1_sel_hss2 = 1;
        }
    }

    if (gb_datapath_master[lchip].quadmac_cfg[0].valid)
    {
        temp = gb_datapath_master[lchip].quadmac_cfg[0].serdes_id;
        if ((temp >= 16) && (temp < 24))
        {
            gb_datapath_master[lchip].hss_cfg.qsgmii0_sel_hss2 = 1;
        }
    }

    return DRV_E_NONE;
}

/**
@brief chip pll init
*/
int32
drv_greatbelt_init_pll(uint8 lchip)
{
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint32 temp =0;
    uint32 hss_coreclk_sel = 0xffffffff;
    uint32 hss_intfclk_sel = 0xffffffff;
    uint32 check_value = 0x01;
    uint32 access_check_value = 0;
    uint8 lane_idx = 0;
    uint8 serdes_mode = 0;
    uint8 use_xfi = 0;

    /* reset hsspll */
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x21f);

    /* powerdown hss 0 */
    DATAPATH_READ_CHIP(lchip, (0x00000100), &temp);
    temp |= 1<<2;
    DATAPATH_WRITE_CHIP(lchip, (0x00000100), temp);

    /* powerdown hss 1 */
    DATAPATH_READ_CHIP(lchip, (0x00000110), &temp);
    temp |= 1<<2;
    DATAPATH_WRITE_CHIP(lchip, (0x00000110), temp);

    /* powerdown hss 2 */
    DATAPATH_READ_CHIP(lchip, (0x00000120), &temp);
    temp |= 1<<2;
    DATAPATH_WRITE_CHIP(lchip, (0x00000120), temp);

    /* powerdown hss 3 */
    DATAPATH_READ_CHIP(lchip, (0x00000130), &temp);
    temp |= 1<<2;
    DATAPATH_WRITE_CHIP(lchip, (0x00000130), temp);

    /* gateclock disable */
    DATAPATH_WRITE_CHIP(lchip, (0x000000c0), 0x0);
    DATAPATH_WRITE_CHIP(lchip, (0x000000c4), 0x0);
    DATAPATH_WRITE_CHIP(lchip, (0x000000c8), 0x0);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x0);


    DATAPATH_WRITE_CHIP(lchip, 0x00000380, gb_datapath_master[lchip].pll_cfg.core_pll.core_pll_cfg1);
    DATAPATH_WRITE_CHIP(lchip, 0x00000384, gb_datapath_master[lchip].pll_cfg.core_pll.core_pll_cfg2);
    DATAPATH_WRITE_CHIP(lchip, 0x00000388, gb_datapath_master[lchip].pll_cfg.core_pll.core_pll_cfg3);
    DATAPATH_WRITE_CHIP(lchip, 0x0000038c, gb_datapath_master[lchip].pll_cfg.core_pll.core_pll_cfg4);
    DATAPATH_WRITE_CHIP(lchip, 0x00000390, gb_datapath_master[lchip].pll_cfg.core_pll.core_pll_cfg5);

    /* need config TankPll, for sgmii mode and xfi clock */
    DATAPATH_WRITE_CHIP(lchip, 0xf8, gb_datapath_master[lchip].pll_cfg.tank_pll.tank_pll_cfg1);
    DATAPATH_WRITE_CHIP(lchip, 0xfc, gb_datapath_master[lchip].pll_cfg.tank_pll.tank_pll_cfg2);

    if ( gb_datapath_master[lchip].pll_cfg.tank_pll.is_used)
    {
        /* release tank pll reset */
    }

    /* config AuxClkSel per lane */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        if (gb_datapath_master[lchip].pll_cfg.hss_pll[index].aux_sel)
        {
            for (sub_idx = 0; sub_idx < 8; sub_idx++)
            {
                /* for sgmii mode and speed is 1G, select auxsel clock */
                if ((gb_datapath_master[lchip].serdes_cfg[index*8+sub_idx].mode == DRV_SERDES_SGMII_MODE) ||
                      (gb_datapath_master[lchip].serdes_cfg[index*8+sub_idx].mode == DRV_SERDES_XGSGMII_MODE) ||
                      (gb_datapath_master[lchip].serdes_cfg[index*8+sub_idx].mode == DRV_SERDES_NA_SGMII_MODE))
                {
                    if (gb_datapath_master[lchip].serdes_cfg[index*8+sub_idx].speed != DRV_SERDES_SPPED_1DOT25G)
                    {
                        return DRV_E_DATAPATH_HSS_INVALID_RATE;
                    }

                    hss_coreclk_sel &= ~(1 <<(index*8+sub_idx));
                    hss_intfclk_sel &= ~(1<<(index*8+sub_idx));
                }
            }
        }

        for (sub_idx = 0; sub_idx < 8; sub_idx++)
        {
            /* xfi core clock use auxsel */
            if (gb_datapath_master[lchip].serdes_cfg[index*8+sub_idx].mode == DRV_SERDES_XFI_MODE)
            {
                hss_coreclk_sel &= ~(1 <<(index*8+sub_idx));
            }
        }
    }

    /* AuxClkSelCfg */
    DATAPATH_WRITE_CHIP(lchip, 0x268, hss_coreclk_sel);
    DATAPATH_WRITE_CHIP(lchip, 0x26c, hss_intfclk_sel);

    /* configure hss12g internal PLL */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        if (gb_datapath_master[lchip].pll_cfg.hss_pll[index].is_used)
        {
            check_value |= (1<<(4+index));
            access_check_value |= (1<<(16+index));
            DATAPATH_WRITE_CHIP(lchip, (0x00000100+index*0x10), gb_datapath_master[lchip].pll_cfg.hss_pll[index].hss_pll_cfg1);
            DATAPATH_WRITE_CHIP(lchip, (0x00000104+index*0x10), gb_datapath_master[lchip].pll_cfg.hss_pll[index].hss_pll_cfg2);
        }
        else
        {
            /* hss is not used powerdown */
            DATAPATH_READ_CHIP(lchip, (0x00000100+index*0x10), &temp);
            temp |= 1<<2;
            DATAPATH_WRITE_CHIP(lchip, (0x00000100+index*0x10), temp);
        }
    }

    /* release the reset of both PLL and HSS */
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x0000020f);

    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x0000010f);

    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x00000107);

    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x00000103);

    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x00000101);

    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, 0x00000008, 0x00000100);

    /* check if the PLLLockOut , check pllcore, pllhss0,1,2,3 lock status */
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_READ_CHIP(lchip, 0x0000000c, &value);
    if ((value & check_value) != check_value)
    {
        DRV_DBG_INFO("PLLLockOut status:0x%x, check_value:0x%x \n", value, check_value);
        return DRV_E_DATAPATH_HSS_PLL_LOCK_FAIL;
    }

    /* cfg ClkDivAuxCfg only usefull when in mix mode */
    DATAPATH_WRITE_CHIP(lchip, 0x260, 0x402);

    /* enable misc interface clock */
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x01000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x03000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x07000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x0f000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x1f000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x3f000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0x7f000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);
    DATAPATH_WRITE_CHIP(lchip, (0x000000cc), 0xff000000);
    sal_task_sleep(DATAPATH_SLEEP_TIME);

    /* check if the HSS access ready , check hss0,1,2,3 ready status */
    DATAPATH_READ_CHIP(lchip, 0x000000e4, &value);
    if ((value & access_check_value) != access_check_value)
    {
        DRV_DBG_INFO("HSS access status:0x%x, check_value:0x%x \n", value, access_check_value);
        return DRV_E_ACCESS_HSS12G_FAIL;
    }

    /* fix xfi jitter issue */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        if (gb_datapath_master[lchip].pll_cfg.hss_pll[index].is_used)
        {
            use_xfi = 0;
            for (sub_idx = 0; sub_idx < 8; sub_idx++)
            {
                /* xfi core clock use auxsel */
                if (gb_datapath_master[lchip].serdes_cfg[index*8+sub_idx].mode == DRV_SERDES_XFI_MODE)
                {
                    use_xfi = 1;
                    break;
                }
            }
            if (use_xfi)
            {
                drv_greatbelt_chip_write_hss12g(lchip, index, ((0x10<<6)|0x0a), 7);
            }
            else
            {
                drv_greatbelt_chip_write_hss12g(lchip, index, ((0x10<<6)|0x0a), 4);
            }
        }
    }

    /* cfg serdes power per lane */
    for(index = 0; index < HSS_SERDES_LANE_NUM; index++)
    {
        lane_idx = gb_datapath_master[lchip].serdes_cfg[index].lane_idx;

        if ((gb_datapath_master[lchip].serdes_cfg[index].mode != DRV_SERDES_NONE_MODE) ||
            (gb_datapath_master[lchip].serdes_cfg[index].dynamic))
        {
            /* need power up */
            serdes_mode = gb_datapath_master[lchip].serdes_cfg[index].mode;
            drv_greatbelt_set_hss12g_link_power_up(lchip, index/8, lane_idx, serdes_mode);
        }
        else
        {
            /* power down */
           drv_greatbelt_set_hss12g_link_power_down(lchip, index/8, lane_idx);
        }
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_init_seperate_hss_lane(uint8 lchip, uint8 hssid, uint8 lane_idx, uint32 tx_cfg, uint32 rx_cfg)
{
    uint16 cfg_tmp = 0;
    uint32 rx_lane_addr[8] = {0x2, 0x3, 0x6, 0x7, 0x0a, 0x0b, 0x0e, 0x0f};
    uint32 tx_lane_addr[8] = {0x0, 0x1, 0x4, 0x5, 0x08, 0x09, 0x0c, 0x0d};

    if (lane_idx >= 8)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    /* cofig reg 0 rx lane */
    DATAPATH_READ_HSS12G(lchip, hssid, ((rx_lane_addr[lane_idx]<<6)|0), &cfg_tmp);
    cfg_tmp = (cfg_tmp & HSS_INTERNEL_LANE_RX_REG0_MASK ) | rx_cfg;
    DATAPATH_WRITE_HSS12G(lchip, hssid, ((rx_lane_addr[lane_idx]<<6)|0), cfg_tmp);

    /* config reg 0 tx lane */
    DATAPATH_READ_HSS12G(lchip, hssid, ((tx_lane_addr[lane_idx]<<6)|0), &cfg_tmp);
    cfg_tmp = (cfg_tmp & HSS_INTERNEL_LANE_TX_REG0_MASK ) | tx_cfg;
    DATAPATH_WRITE_HSS12G(lchip, hssid, ((tx_lane_addr[lane_idx]<<6)|0), cfg_tmp);

    /* config reg2 bit4 tx Equalization */
    DATAPATH_READ_HSS12G(lchip, hssid, ((tx_lane_addr[lane_idx]<<6)|2), &cfg_tmp);
    cfg_tmp |= 0x10;
    DATAPATH_WRITE_HSS12G(lchip, hssid, ((tx_lane_addr[lane_idx]<<6)|2), cfg_tmp);

    return DRV_E_NONE;
}

/**
@brief chip hss12g init
*/
int32
drv_greatbelt_init_hss(uint8 lchip)
{
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint8 lane_idx = 0;
    uint8 serdes_idx = 0;
    uint32 hss_mode = 0;
    uint32 rx_cfg = 0;
    uint32 tx_cfg = 0;
    uint32 int_clk_div = 0;
    uint32 core_clk_div = 0;
    uint32 hss_mode_cfg = 0;
    int32 ret = 0;
    uint32 hss_bit_order = 0;

    /* config Tx/Rx interface signal and internal register */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
         /*-DATAPATH_WRITE_CHIP(lchip, (0x00000104+index*0x10), 0x0100ff00);*/

        /* config every lane seperately */
        for (sub_idx = 0; sub_idx < 8; sub_idx++)
        {
            serdes_idx = index*8 + sub_idx;
            lane_idx = gb_datapath_master[lchip].serdes_cfg[serdes_idx].lane_idx;
            drv_greatbelt_datapath_get_hss_internel_reg_cfg(lchip, index, serdes_idx, &tx_cfg, &rx_cfg);
            drv_greatbelt_init_seperate_hss_lane(lchip, index, lane_idx, tx_cfg, rx_cfg);
        }
    }

    /* hss clkdivider */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        hss_mode = gb_datapath_master[lchip].hss_cfg.hss_mode[index];
        if (DRV_SERDES_NONE_MODE != hss_mode)
        {
            if (DRV_SERDES_QSGMII_MODE == hss_mode)
            {
                int_clk_div = 0x00000102;
                core_clk_div = 0x00000002;
            }
            else if ((DRV_SERDES_SGMII_MODE == hss_mode) || (DRV_SERDES_XGSGMII_MODE == hss_mode))
            {
                if (gb_datapath_master[lchip].pll_cfg.hss_pll[index].aux_sel)
                {

                    int_clk_div = 0x00000002;
                    core_clk_div = 0x00000002;

                    /* tank reference clock is 156: high 8 bit of PLL CFG1 is 0xd8
                     * tank reference clock is 125: high 8 bit of PLL CFG1 is 0xde
                     */
                    if ((0xd8 == ((gb_datapath_master[lchip].pll_cfg.hss_pll[index].hss_pll_cfg1) >> 8)) ||
                        (0xde == ((gb_datapath_master[lchip].pll_cfg.hss_pll[index].hss_pll_cfg1) >> 8)))
                    {
                        /* mix mode with QSGMII */
                        int_clk_div = 0x00000102;
                        core_clk_div = 0x00000002;
                    }
                }
                else
                {
                    int_clk_div = 0x00000702;
                    core_clk_div = 0x00000302;
                }
            }
            else if (DRV_SERDES_XFI_MODE == hss_mode)
            {
                int_clk_div = 0x00000002;
                core_clk_div = 0x00000002;
            }
            else if (DRV_SERDES_XAUI_MODE == hss_mode)
            {
                for (sub_idx = 0; sub_idx < 8; sub_idx++)
                {
                    serdes_idx = index*8 + sub_idx;

                    if (DRV_SERDES_SPPED_3DOT125G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        int_clk_div  = 0x00000302;
                        core_clk_div = 0x00000102;
                    }
                    else if (DRV_SERDES_SPPED_5G15625G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        int_clk_div  = 0x00000102;
                        core_clk_div = 0x00000002;
                    }
                    else if (DRV_SERDES_SPPED_6DOT25G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        int_clk_div  = 0x00000102;
                        core_clk_div = 0x00000002;
                    }
                }

            }
            else if (DRV_SERDES_INTLK_MODE == hss_mode)
            {
                for (sub_idx = 0; sub_idx < 8; sub_idx++)
                {
                    serdes_idx = index*8 + sub_idx;
                    if (DRV_SERDES_SPPED_3DOT125G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        int_clk_div  = 0x00000302;
                        core_clk_div = 0x00000102;
                    }
                    else if (DRV_SERDES_SPPED_6DOT25G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        int_clk_div  = 0x00000102;
                        core_clk_div = 0x00000002;
                    }
                    else if (DRV_SERDES_SPPED_10DOT3125G == gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed)
                    {
                        int_clk_div  = 0x00000002;
                        core_clk_div = 0x00000002;
                    }
                }
            }
            else if (DRV_SERDES_MIX_MODE_WITH_QSGMII == hss_mode)
            {
                /* SGMII(XSGMII) with QSGMII Mix Mode*/
                int_clk_div = 0x00000102;
                core_clk_div = 0x00000002;
            }
            else if (DRV_SERDES_MIX_MODE_WITH_XFI == hss_mode)
            {
                /* SGMII(XSGMII) with XFI Mix Mode*/
                int_clk_div = 0x00000002;
                core_clk_div = 0x00000002;
            }
            else if (DRV_SERDES_2DOT5_MODE == hss_mode)
            {
                /*for 2.5g*/
                int_clk_div  = 0x00000302;
                core_clk_div = 0x00000102;
            }

            /* ClkDivIntf0Cfg */
            DATAPATH_WRITE_CHIP(lchip, (0x00000240+index*8), int_clk_div);
            /* ClkDivCore0Cfg */
            DATAPATH_WRITE_CHIP(lchip, (0x00000244+index*8), core_clk_div);
        }
    }

    /* release all of the ClkDivReset */
    DATAPATH_WRITE_CHIP(lchip, 0x00000264, 0);

    /* Configure HSS Lane Mode */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        hss_mode_cfg = 0;

        for (lane_idx = 0; lane_idx < 8; lane_idx++)
        {
            serdes_idx = index*8 + lane_idx;

            if (gb_datapath_master[lchip].serdes_cfg[serdes_idx].mode == DRV_SERDES_2DOT5_MODE)
            {
                if(gb_datapath_master[lchip].serdes_cfg[serdes_idx].mac_id < 48)
                {
                    hss_mode_cfg |=  (DRV_SERDES_SGMII_MODE << (lane_idx*4));
                }
                else
                {
                    hss_mode_cfg |=  (DRV_SERDES_XGSGMII_MODE << (lane_idx*4));
                }
            }
            else
            {
                hss_mode_cfg |=  (gb_datapath_master[lchip].serdes_cfg[serdes_idx].mode << (lane_idx*4));
            }
        }

         DATAPATH_WRITE_CHIP(lchip, (0x000001d0+index*4), hss_mode_cfg);
    }

    /* config sgmac mode */
    ret = drv_greatbelt_pll_sgmac_init(lchip);
    if (ret < 0)
    {
        return ret;
    }

    /* QsgmiiHssSelCfg */
    ret = drv_greatbelt_pll_qsgmii_init(lchip);
    if (ret < 0)
    {
        return ret;
    }

    /* HssTxGearBoxRstCtl */
    DATAPATH_WRITE_CHIP(lchip, 0x00000140, 0);

    /* HssBitOrderCfg for interlaken */
    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        if (gb_datapath_master[lchip].hss_cfg.hss_mode[index] == DRV_SERDES_INTLK_MODE)
        {
            hss_bit_order |= (0xff << (index*8));
        }
    }

    DATAPATH_WRITE_CHIP(lchip, 0x000001c8, hss_bit_order);

    return DRV_E_NONE;
}

int32
drv_greatbelt_init_cpumac(uint8 lchip)
{
    uint32 cpu_misc  = 0;
    uint8 speed_mode = 0;
    /* config hss6g */
    DATAPATH_WRITE_CHIP(lchip, 0x190, 0x884100f1);
    DATAPATH_WRITE_CHIP(lchip, 0x194, 0x00f0224);

    /*cfg cpumac pcs autoneg, CpuMacPcsLinkTimerCtl*/
    DATAPATH_WRITE_CHIP(lchip, 0xca00a4, 0x803d0900);
    /* CpuMacPcsAnegCfg, pcsSpeedMode=2, pcsIgnoreLinkFailure=1, pcsAnEnable=1 */
    DATAPATH_WRITE_CHIP(lchip, 0xca0078, 0x241);

    /* CpuMacMiscCtl, 0x00ca0014 */
    if (DRV_CPU_MAC_TYPE_NA != gb_datapath_master[lchip].misc_info.cpumac_type)
    {
        switch (gb_datapath_master[lchip].misc_info.cpumac_type)
        {
            case DRV_CPU_MAC_TYPE_10M:
                speed_mode = 0;
                break;
            case DRV_CPU_MAC_TYPE_100M:
                speed_mode = 1;
                break;
            case DRV_CPU_MAC_TYPE_GE:
                speed_mode = 2;
                break;
            default:
                speed_mode = 2;
                break;
        }

        /* cpumac init, CpuMacInitCtl */
        DATAPATH_WRITE_CHIP(lchip, 0xca0018, 0x1);
        /* CpuMacResetCtl */
        DATAPATH_WRITE_CHIP(lchip, 0xca0010, 0x0);

        /* set cpu mac speed */
        DATAPATH_READ_CHIP(lchip,  0x00ca0014, &cpu_misc);
        DATAPATH_WRITE_CHIP(lchip, 0x00ca0014, ((cpu_misc&0xFFFCFFFF) | ((speed_mode&0x3)<<16)));
    }

    return DRV_E_NONE;
}

/*
 * set force signal force defect when mac speed > 3.125G
*/
int32
drv_greatbelt_datapath_force_signal_detect(uint8 lchip, bool enable)
{
    uint32 cmd = 0;
    uint32 tbl_id = 0;
    uint32 field_value = 0;
    uint8 lport = 0;
    uint8 mac_id = 0;
    uint8 pcs_mode = 0;
    uint8 speed_mode = 0;

    drv_datapath_port_capability_t port_cap;

    field_value = (TRUE == enable) ? 1 : 0;

    for (lport = 0; lport < MAC_NUM; lport++)
    {
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);
        if (!(port_cap.valid))
        {
            continue;
        }

        mac_id = port_cap.mac_id;
        pcs_mode = port_cap.pcs_mode;
        speed_mode = port_cap.speed_mode;

        if (speed_mode <= DRV_SERDES_SPPED_3DOT125G)
        {
            continue;
        }

        switch(pcs_mode)
        {
            case DRV_SERDES_QSGMII_MODE:
                 tbl_id = QsgmiiPcsCfg0_t + (mac_id / 4);
                 cmd = DRV_IOW(tbl_id, QsgmiiPcsCfg_ForceSignalDetect_f);
                 DRV_IOCTL(lchip, 0, cmd, &field_value);
                 break;

            case DRV_SERDES_XAUI_MODE:
            case DRV_SERDES_XFI_MODE:
                tbl_id  = SgmacPcsCfg0_t + (mac_id - GMAC_NUM);
                cmd = DRV_IOW(tbl_id, SgmacPcsCfg_ForceSignalDetect_f);
                DRV_IOCTL(lchip, 0, cmd, &field_value);
                break;

            case DRV_SERDES_INTLK_MODE:
                 tbl_id = IntLkLaneRxCtl_t;
                 cmd = DRV_IOW(tbl_id, IntLkLaneRxCtl_LaneRxForceSignalDetect_f);
                 DRV_IOCTL(lchip, 0, cmd, &field_value);
                 break;

            default:
                 break;
        }

    }

    return DRV_E_NONE;
}


int32
drv_greatbelt_init_mac(uint8 lchip)
{
    uint32 cmd = 0;
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint8 mac_id = 0;
    uint8 lport = 0;
    uint8 sgmac_id = 0;
    uint32 tbl_id = 0;
    qsgmii_pcs_soft_rst_t qsgmii_soft_rst;
    drv_datapath_port_capability_t port_cap;

    sal_memset(&qsgmii_soft_rst, 0, sizeof(qsgmii_pcs_soft_rst_t));
    sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));

    field_value = 0x1e848;
    cmd = DRV_IOW(LinkTimerCtl_t, LinkTimerCtl_LinkTimerCnt_f);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

    /*enable valid mac, init ModuleGatedClkCtl for mac and ref pcs */
    for (lport = 0; lport < MAC_NUM; lport++)
    {
        drv_greatbelt_get_port_capability(lchip, lport, &port_cap);

        if (port_cap.valid)
        {
            mac_id = port_cap.mac_id;
            /* for gmac */
            if (mac_id < GMAC_NUM)
            {
                /* gmac module gate */
                field_id  = ModuleGatedClkCtl_EnClkSupGmac0_f + mac_id;
                field_value = 1;
                cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                DRV_IOCTL(lchip, 0, cmd, &field_value);

                if (mac_id < 24)
                {
                    /* sgmii pcs module gate */
                    field_id  = ModuleGatedClkCtl_EnClkSupPcs0_f + mac_id;
                    field_value = 1;
                    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                    DRV_IOCTL(lchip, 0, cmd, &field_value);

                    /* release quad mac pcs */
                    field_id = ResetIntRelated_ResetQuadPcs0Reg_f + (mac_id / 4);
                    field_value = 0;
                    cmd = DRV_IOW(ResetIntRelated_t, field_id);
                    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
                }

                /* quad pcs module gate */
                field_id  = ModuleGatedClkCtl_EnClkSupQsgmii0_f + mac_id/4;
                field_value = 1;
                cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                DRV_IOCTL(lchip, 0, cmd, &field_value);

                /* release qsgmii reg reset */
                field_id = ResetIntRelated_ResetQsgmii0Reg_f + 2*(mac_id / 4);
                field_value = 0;
                cmd = DRV_IOW(ResetIntRelated_t, field_id);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                /* release qsgmii reset */
                field_id = ResetIntRelated_ResetQsgmii0_f + 2*(mac_id / 4);
                field_value = 0;
                cmd = DRV_IOW(ResetIntRelated_t, field_id);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));


                tbl_id = QsgmiiPcsSoftRst0_t + (mac_id / 4);
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &qsgmii_soft_rst));


                /* release quad mac app reset */
                field_id = ResetIntRelated_ResetQuadMac0App_f + 2*(mac_id / 4);
                field_value = 0;
                cmd = DRV_IOW(ResetIntRelated_t, field_id);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                /* release quad mac reset */
                field_id = ResetIntRelated_ResetQuadMac0Reg_f + 2*(mac_id / 4);
                field_value = 0;
                cmd = DRV_IOW(ResetIntRelated_t, field_id);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                /* init quadmac stats*/
                tbl_id = QuadMacInit0_t + (mac_id / 4);
                field_value = 0x8f;
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                tbl_id = QuadMacInit0_t + (mac_id / 4);
                field_value = 0x18f;
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                /* ignor link fail if pcs mode is SGMII */
                if ((DRV_SERDES_SGMII_MODE   == port_cap.pcs_mode) ||
                    (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode))
                {
                    tbl_id = QuadPcsPcs0AnegCfg0_t + (mac_id / 4) * (QuadPcsPcs0AnegCfg1_t - QuadPcsPcs0AnegCfg0_t) + (mac_id % 4) * (QuadPcsPcs1AnegCfg0_t - QuadPcsPcs0AnegCfg0_t);
                    field_id = QuadPcsPcs0AnegCfg_Pcs0IgnoreLinkFailure_f;
                    field_value = 1;
                    cmd = DRV_IOW(tbl_id, field_id);
                    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
                }

                /* set pcs0IgnoreLinkFailure */
                tbl_id = QsgmiiPcs0AnegCfg0_t + (mac_id / 4) * (QsgmiiPcs0AnegCfg1_t - QsgmiiPcs0AnegCfg0_t) + (mac_id % 4) * (QsgmiiPcs1AnegCfg0_t - QsgmiiPcs0AnegCfg0_t);
                field_id = QuadPcsPcs0AnegCfg_Pcs0IgnoreLinkFailure_f;
                field_value = 1;
                cmd = DRV_IOW(tbl_id, field_id);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
            }
            else
            {
                /* for sgmac */
                sgmac_id = mac_id - GMAC_NUM;
                if (sgmac_id >= 4)
                {
                    /*enClkSupSgmac4XgmacPcs for xaui */
                    field_id  = ModuleGatedClkCtl_EnClkSupSgmac4XgmacPcs_f + (sgmac_id-4)*3;
                    field_value = 1;
                    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                    DRV_IOCTL(lchip, 0, cmd, &field_value);

                    /*ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f for xsgmii */
                    field_id  = ModuleGatedClkCtl_EnClkSupSgmac4Pcs_f + (sgmac_id-4)*3;
                    field_value = 1;
                    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                    DRV_IOCTL(lchip, 0, cmd, &field_value);

                    /*ModuleGatedClkCtl_EnClkSupSgmac0Xfi_f for xfi */
                    field_id  = ModuleGatedClkCtl_EnClkSupSgmac4Xfi_f + (sgmac_id-4)*3;
                    field_value = 1;
                    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                    DRV_IOCTL(lchip, 0, cmd, &field_value);
                }
                else
                {
                    /*ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f for xsgmii */
                    field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Pcs_f + sgmac_id*2;
                    field_value = 1;
                    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                    DRV_IOCTL(lchip, 0, cmd, &field_value);

                    /*ModuleGatedClkCtl_EnClkSupSgmac0Xfi_f for xfi */
                    field_id  = ModuleGatedClkCtl_EnClkSupSgmac0Xfi_f + sgmac_id*2;
                    field_value = 1;
                    cmd = DRV_IOW(ModuleGatedClkCtl_t, field_id);
                    DRV_IOCTL(lchip, 0, cmd, &field_value);
                }

                /* release sgmac reg */
                field_id = ResetIntRelated_ResetSgmac0Reg_f + 2*sgmac_id ;
                field_value = 0;
                cmd = DRV_IOW(ResetIntRelated_t, field_id);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));



                /* init sgmac stats */
                tbl_id = SgmacStatsInit0_t + sgmac_id;
                field_value = 0;
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                tbl_id = SgmacStatsInit0_t + sgmac_id;
                field_value = 1;
                cmd = DRV_IOW(tbl_id, DRV_ENTRY_FLAG);
                DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));

                if (DRV_SERDES_XGSGMII_MODE == port_cap.pcs_mode)
                {
                    tbl_id = SgmacPcsAnegCfg0_t + sgmac_id;
                    field_id = SgmacPcsAnegCfg_IgnoreLinkFailure_f;
                    field_value = 1;
                    cmd = DRV_IOW(tbl_id, field_id);
                    DRV_IF_ERROR_RETURN(DRV_IOCTL(lchip, 0, cmd, &field_value));
                }

                /* SgmacXauiCfg0-11 */
                DATAPATH_READ_CHIP(lchip, (0x00400030 + sgmac_id*0x10000), &field_value);
                if(gb_datapath_master[lchip].ignore_mode)
                {
                    field_value |= (7 << 1);
                }
                else
                {
                    field_value &= ~(7 << 1);
                }
                DATAPATH_WRITE_CHIP(lchip, (0x00400030 + sgmac_id*0x10000), field_value);

            }
        }
    }

    /* init force signal force defect when mac speed > 3.125G */
    DRV_IF_ERROR_RETURN(drv_greatbelt_datapath_force_signal_detect(lchip, FALSE));

    return DRV_E_NONE;
}

int32
drv_greatbelt_init_led(uint8 chip_id)
{
    uint32 field_value = 0;
    uint32 field_id = 0;
    uint32 cmd = 0;
    uint32 clock_core = 0;
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint8 lane_idx = 0;
    uint8 serdes_idx = 0;
    uint32 rx_lane_addr[8] = {0x02, 0x03, 0x06, 0x07, 0x0a, 0x0b, 0x0e, 0x0f};
    uint16 value = 0;

    field_id = ResetIntRelated_ResetLed_f;
    field_value = 1;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));
    field_id = LedClockDiv_LedClkDiv_f;

    clock_core = drv_greatbelt_get_clock(chip_id, DRV_CORE_CLOCK_TYPE);

    field_value = clock_core/15;
    cmd = DRV_IOW(LedClockDiv_t, field_id);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));

    field_id = LedClockDiv_ResetLedClkDiv_f;
    field_value = 0;
    cmd = DRV_IOW(LedClockDiv_t, field_id);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));

    field_id = ResetIntRelated_ResetLed_f;
    field_value = 0;
    cmd = DRV_IOW(ResetIntRelated_t, field_id);
    DRV_IF_ERROR_RETURN(DRV_IOCTL(chip_id, 0, cmd, &field_value));

    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        for (sub_idx = 0; sub_idx < 8; sub_idx++)
        {
            serdes_idx = index*8 + sub_idx;
            lane_idx = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].lane_idx;

            if (gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode == DRV_SERDES_XFI_MODE)
            {
                /*enable dpc*/
                value = 0xffff;
                drv_greatbelt_chip_write_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0x1f), value);
            }
            if (gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode == DRV_SERDES_XAUI_MODE)
            {
                /*pole*/
                drv_greatbelt_chip_read_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0xb), &value);
                value |= (1<<13);
                value &= (~(1<<12));
                drv_greatbelt_chip_write_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0xb), value);
            }

            if (gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode == DRV_SERDES_QSGMII_MODE)
            {
                value = 0x6ffb;
                drv_greatbelt_chip_write_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0x1f), value);
                /*DFE NRZ*/
                drv_greatbelt_chip_read_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0x0), &value);
                value |= (1<<6);
                value &= (~(1<<5));
                value &= (~(1<<4));
                drv_greatbelt_chip_write_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0x0), value);
                /*pole peak*/
                drv_greatbelt_chip_read_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0xb), &value);
                value |= (1<<13);
                value &= (~(1<<12));
                value &= (~(1<<10));
                value &= (~(1<<9));
                value |= (1<<8);
                drv_greatbelt_chip_write_hss12g(chip_id, index, ((rx_lane_addr[lane_idx]<<6)|0xb), value);
            }
        }

    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_parse_datapath_file(char* datapath_config_file)
{
    int32 ret = 0;
     uint8 chip_id = 0;

    DRV_PTR_VALID_CHECK(datapath_config_file);

    ret = drv_greatbelt_datapath_global_init();
    if (ret < 0)
    {
        return ret;
    }

    /* 1. read datapath profile file */
    ret = drv_greatbelt_read_datapath_profile((char*)datapath_config_file);
    if (ret < 0)
    {
        return ret;
    }

    chip_id = g_gb_chip_para.seq;

    /* 1. get datapath information */
     ret = drv_greatbelt_get_datapath_info(chip_id);
    if (ret < 0)
    {
        return ret;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_init_pll_hss(void)
{
    int32 ret = 0;
    uint8 chip_id = 0;
    uint32 cmd = 0;
    int32 init_done   = 0;

    chip_id = g_gb_chip_para.seq;
    cmd = DRV_IOR(PllLockOut_t, PllLockOut_CorePllLock_f);
    (DRV_IOCTL(chip_id, 0, cmd, &init_done));
    if (init_done)
    {
        return DRV_E_NONE;
    }

    /* 1. pll init */
    ret = drv_greatbelt_init_pll(chip_id);
    if (ret < 0)
    {
        return ret;
    }

    /* 2. hss12g init */
    ret = drv_greatbelt_init_hss(chip_id);
    if (ret < 0)
    {
        return DRV_E_DATAPATH_INIT_HSS_FAIL;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_init_total(void)
{
    int32 ret = 0;
    uint8 chip_id = 0;

    chip_id = g_gb_chip_para.seq;

    /* 1. datapath init */
    ret = drv_greatbelt_init_datapath(chip_id);
    if (ret < 0)
    {
        return DRV_E_DATAPATH_INIT_DATAPATH_FAIL;
    }

    /* 2. cpumac init */
    ret = drv_greatbelt_init_cpumac(chip_id);
    if (ret < 0)
    {
        return ret;
    }

    /* 3. gmac init: no need to do , RxCtl/TxCtl/MacInit */
    ret = drv_greatbelt_init_mac(chip_id);
    if (ret < 0)
    {
        return ret;
    }

    /* 4. init mac led */
    ret = drv_greatbelt_init_led(chip_id);
    if (ret < 0)
    {
        return ret;
    }

    return DRV_E_NONE;
}


STATIC uint8
drv_greatbelt_get_free_chan(uint8 lchip)
{
    uint8 chan_id = 0;
    uint8 free_chan = INVALID_CHAN_ID;

    for (chan_id = 0; chan_id < CHAN_NUM; chan_id++)
    {
        if (!gb_datapath_master[lchip].chan_used[chan_id])
        {
            free_chan = chan_id;
			gb_datapath_master[lchip].chan_used[chan_id] = 1;
            return free_chan;
        }
    }
    return free_chan;
}

int32
drv_greatbelt_set_hss_info(uint8 chip_id, uint8 hss_id, drv_chip_hss_info_type_t info_type, void* p_hss_info)
{
    if (hss_id >= HSS_MACRO_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    switch(info_type)
    {
        case DRV_CHIP_HSS_MODE_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_mode[hss_id]= *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_SGMAC4_INFO:
            gb_datapath_master[chip_id].hss_cfg.sgmac4_is_xaui= *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_SGMAC5_INFO:
            gb_datapath_master[chip_id].hss_cfg.sgmac5_is_xaui= *((uint8*)p_hss_info);
            break;
         case DRV_CHIP_HSS_SGMAC6_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_used_sgmac6 = *((uint8*)p_hss_info);
            break;
       case DRV_CHIP_HSS_SGMAC7_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_used_sgmac7 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_SGMAC8_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_used_sgmac8 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_SGMAC9_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_used_sgmac9 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_SGMAC10_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_used_sgmac10 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_SGMAC11_INFO:
            gb_datapath_master[chip_id].hss_cfg.hss_used_sgmac11 = *((uint8*)p_hss_info);
            break;
         case DRV_CHIP_HSS_QSGMII0_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii0_sel_hss2 = *((uint8*)p_hss_info);
            break;
       case DRV_CHIP_HSS_QSGMII1_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii1_sel_hss2 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_QSGMII2_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii2_sel_hss3 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_QSGMII3_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii3_sel_hss3 = *((uint8*)p_hss_info);
            break;
         case DRV_CHIP_HSS_QSGMII6_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii6_sel_hss2 = *((uint8*)p_hss_info);
            break;
       case DRV_CHIP_HSS_QSGMII7_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii7_sel_hss2 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_QSGMII8_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii8_sel_hss3 = *((uint8*)p_hss_info);
            break;
        case DRV_CHIP_HSS_QSGMII9_INFO:
            gb_datapath_master[chip_id].hss_cfg.qsgmii9_sel_hss3 = *((uint8*)p_hss_info);
            break;
        default:
            break;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_get_serdes_info(uint8 chip_id, uint8 serdes_idx, drv_chip_serdes_info_type_t info_type,
                                            void* p_serdes_info)
{

    if (serdes_idx >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    if (gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode == 0xff)
    {
        return DRV_E_HSS_LANE_IS_NOT_USED;
    }

    switch(info_type)
    {
        case DRV_CHIP_SERDES_MODE_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode;
            break;
        case DRV_CHIP_SERDES_SPEED_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].speed;
            break;
        case DRV_CHIP_SERDES_MACID_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mac_id;
            break;
        case DRV_CHIP_SERDES_CHANNEL_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].chan_id;
            break;
        case DRV_CHIP_SERDES_HSSID_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].hss_id;
            break;
        case DRV_CHIP_SERDES_LANE_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].lane_idx;
            break;
        case DRV_CHIP_SERDES_SWITCH_INFO:
            *((uint8*)p_serdes_info) = gb_datapath_master[chip_id].serdes_cfg[serdes_idx].dynamic;
            break;
        case DRV_CHIP_SERDES_ALL_INFO:
            sal_memcpy((drv_datapath_serdes_info_t*)p_serdes_info, &(gb_datapath_master[chip_id].serdes_cfg[serdes_idx]), sizeof(drv_datapath_serdes_info_t));
            break;
        default:
            break;
    }

    return DRV_E_NONE;
}

int32
drv_greatbelt_set_serdes_info(uint8 chip_id, uint8 serdes_idx, drv_chip_serdes_info_type_t info_type, void* p_info)
{
    if (serdes_idx >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    if (gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode == 0xff)
    {
        return DRV_E_HSS_LANE_IS_NOT_USED;
    }

    switch(info_type)
    {
        case DRV_CHIP_SERDES_MODE_INFO:
            gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mode = *((uint8*)p_info);
            break;
        case DRV_CHIP_SERDES_SPEED_INFO:
            gb_datapath_master[chip_id].serdes_cfg[serdes_idx].speed = *((uint8*)p_info);
            break;
        case DRV_CHIP_SERDES_MACID_INFO:
            gb_datapath_master[chip_id].serdes_cfg[serdes_idx].mac_id = *((uint8*)p_info);
            break;
        case DRV_CHIP_SERDES_CHANNEL_INFO:
            gb_datapath_master[chip_id].serdes_cfg[serdes_idx].chan_id= *((uint8*)p_info);
            break;
        case DRV_CHIP_SERDES_HSSID_INFO:
            gb_datapath_master[chip_id].serdes_cfg[serdes_idx].hss_id = *((uint8*)p_info);
            break;
        case DRV_CHIP_SERDES_LANE_INFO:
            gb_datapath_master[chip_id].serdes_cfg[serdes_idx].lane_idx = *((uint8*)p_info);
            break;
        case DRV_CHIP_SERDES_ALL_INFO:
            sal_memcpy(&(gb_datapath_master[chip_id].serdes_cfg[serdes_idx]), p_info, sizeof(drv_datapath_serdes_info_t));
            break;
        default:
            break;
    }

    return DRV_E_NONE;
}

uint32
drv_greatbelt_get_clock(uint8 chip_id, drv_datapath_clock_type_t clock_type)
{
    uint32 clock = 0;

    switch(clock_type)
    {
        case DRV_CORE_CLOCK_TYPE:
            clock = gb_datapath_master[chip_id].pll_cfg.core_pll.output_a;
            break;

        case DRV_CORE_REF_CLOCK_TYPE:
            clock = gb_datapath_master[chip_id].pll_cfg.core_pll.ref_clk;
            break;

        case DRV_TANK_CLOCK_TYPE:
            clock = gb_datapath_master[chip_id].pll_cfg.tank_pll.output_a;
            break;

        case DRV_TANK_REF_CLOCK_TYPE:
            clock = gb_datapath_master[chip_id].pll_cfg.tank_pll.ref_clk;
            break;

        default:
            break;
    }

    return clock;
}



int32
drv_greatbelt_datapath_sim_init()
{
    uint8 index = 0;
    uint8 lchip = 0;

    drv_greatbelt_datapath_global_init();

    for (lchip = 0; lchip < DRV_MAX_CHIP_NUM; lchip++)
    {
        gb_datapath_master[lchip].pll_cfg.core_pll.output_a = 450;
        for (index = 0; index < QUADMAC_NUM; index++)
        {
            gb_datapath_master[lchip].quadmac_cfg[index].valid = 1;
            gb_datapath_master[lchip].quadmac_cfg[index].pcs_mode = DRV_SERDES_SGMII_MODE;
        }

        for (index = 0; index < SGMAC_NUM; index++)
        {
            gb_datapath_master[lchip].sgmac_cfg[index].valid = 1;
            gb_datapath_master[lchip].sgmac_cfg[index].pcs_mode = DRV_SERDES_XFI_MODE;
        }

        for (index = 0; index < MAC_NUM; index++)
        {
            gb_datapath_master[lchip].port_capability[index].chan_id = index;
            gb_datapath_master[lchip].port_capability[index].valid   = 1;
            gb_datapath_master[lchip].port_capability[index].mac_id  = index;
            if (index < GMAC_NUM)
            {
                gb_datapath_master[lchip].port_capability[index].port_type = DRV_PORT_TYPE_1G;
            }
            else
            {
                gb_datapath_master[lchip].port_capability[index].port_type = DRV_PORT_TYPE_SG;
            }

        }

        drv_greatbelt_datapath_init_internal_chan(lchip);
    }


    return DRV_E_NONE;
}

int32
drv_greatbelt_get_datapath(uint8 lchip, drv_datapath_master_t** p_datapath)
{
    *p_datapath = (drv_datapath_master_t*)&(gb_datapath_master[lchip]);

    return DRV_E_NONE;
}

bool
drv_greatbelt_get_intlk_support(uint8 lchip)
{
    uint8 index = 0;

    for (index = 0; index < HSS_MACRO_NUM; index++)
    {
        if (gb_datapath_master[lchip].hss_cfg.hss_mode[index] == DRV_SERDES_INTLK_MODE)
        {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 @brief hss12g link power down interface
*/
STATIC int32
drv_greatbelt_set_hss12g_link_power_down(uint8 lchip, uint8 hssid, uint8 link_idx)
{
    uint16 temp = 0;
    uint32 port_en_addr = 0;
    uint32 port_reset_addr = 0;
    uint8 rx_port_idx = 0;
    uint8 tx_port_idx = 0;
    uint8 rx_idx_mapping[8] = {2, 3, 6, 7, 10, 11, 14, 15};
    uint8 tx_idx_mapping[8] = {0, 1, 4, 5,  8,  9,  12, 13};

    rx_port_idx = rx_idx_mapping[link_idx];
    tx_port_idx = tx_idx_mapping[link_idx];

    if (rx_port_idx >= 8 )
    {
        port_reset_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_RESET_REG2);
        port_en_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_EN_REG2);
        rx_port_idx -= 8;
        tx_port_idx -= 8;
    }
    else
    {
        port_reset_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_RESET_REG1);
        port_en_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_EN_REG1);
    }
    /* 1. release hss12g port reset */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, port_reset_addr, &temp);

    /*  tx port reset */
    temp |= (1 << tx_port_idx);

    /*  rx port reset */
    temp |= (1 << rx_port_idx);

    drv_greatbelt_chip_write_hss12g(lchip, hssid, port_reset_addr, temp);

    /* 2. set hss12g port enable */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, port_en_addr, &temp);

    /* config tx port disable */
    temp &= (~(1 << tx_port_idx));

    /* config rx port disable */
    temp &= (~(1 << rx_port_idx));

    drv_greatbelt_chip_write_hss12g(lchip, hssid, port_en_addr, temp);

    return 0;
}

/**
 @brief hss12g link power up interface
*/
STATIC int32
drv_greatbelt_set_hss12g_link_power_up(uint8 lchip, uint8 hssid, uint8 link_idx, uint8 serdes_mode)
{
    uint16 temp = 0;
    uint32 port_en_addr = 0;
    uint32 port_reset_addr = 0;
    uint8 rx_idx_mapping[8] = {2, 3, 6, 7, 10, 11, 14, 15};
    uint8 tx_idx_mapping[8] = {0, 1, 4, 5,  8,  9,  12, 13};
    uint8 rx_port_idx = 0;
    uint8 tx_port_idx = 0;
    uint32 test_reg = 0x03;
    uint32 tx_lane_addr[8] = {0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0c, 0x0d};

    rx_port_idx = rx_idx_mapping[link_idx];
    tx_port_idx = tx_idx_mapping[link_idx];

    /* 1. set hss12g port enable */
    if (rx_port_idx >= 8)
    {
        /* mens this link is configed in port_en_reg2*/
        port_en_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_EN_REG2);
        port_reset_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_RESET_REG2);
        rx_port_idx -= 8;
        tx_port_idx -= 8;
    }
    else
    {
        port_en_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_EN_REG1);
        port_reset_addr = DRV_HSS12G_ADDRESS_CONVERT(DRV_HSS12G_PLL_COMMON_HIGH_ADDR, DRV_HSS12G_PORT_RESET_REG1);
    }

    drv_greatbelt_chip_read_hss12g(lchip, hssid, port_en_addr, &temp);

    /* config tx port enable */
    temp |= (1 << tx_port_idx);

    /* config rx port enable */
    temp |= (1 << rx_port_idx);

    drv_greatbelt_chip_write_hss12g(lchip, hssid, port_en_addr, temp);

    /* disable serdes TX */
    if((DRV_SERDES_XAUI_MODE != serdes_mode) && (DRV_SERDES_QSGMII_MODE != serdes_mode))
    {
        drv_greatbelt_chip_read_hss12g(lchip, hssid, (tx_lane_addr[link_idx] << 6)|test_reg, &temp);
        temp |= 0x0020;
        drv_greatbelt_chip_write_hss12g(lchip, hssid, (tx_lane_addr[link_idx] << 6)|test_reg, temp);
    }

    /* 2. release hss12g port reset */
    drv_greatbelt_chip_read_hss12g(lchip, hssid, port_reset_addr, &temp);

    /* release tx port reset */
    temp &= (~(1 << tx_port_idx));

    /* release rx port reset */
    temp &= (~(1 << rx_port_idx));

    drv_greatbelt_chip_write_hss12g(lchip, hssid, port_reset_addr, temp);

    /*delay 100ns */
    sal_udelay(1);

    return 0;
}


uint8
drv_greatbelt_get_port_capability(uint8 lchip, uint8 lport, drv_datapath_port_capability_t *port_cap)
{
    uint8  serdes_idx = 0;
    uint8  div = 1;

    port_cap->valid     = gb_datapath_master[lchip].port_capability[lport].valid;
    if (port_cap->valid)
    {
        port_cap->chan_id   = gb_datapath_master[lchip].port_capability[lport].chan_id;
        port_cap->port_type = gb_datapath_master[lchip].port_capability[lport].port_type;
        port_cap->mac_id    = gb_datapath_master[lchip].port_capability[lport].mac_id;
        port_cap->pcs_mode = DRV_SERDES_NONE_MODE;

        if (lport < MAC_NUM)
        {
            for (serdes_idx = 0; serdes_idx < HSS_SERDES_LANE_NUM; serdes_idx++)
            {
                if (DRV_SERDES_NONE_MODE == gb_datapath_master[lchip].serdes_cfg[serdes_idx].mode )
                {
                    continue;
                }

                if (DRV_SERDES_QSGMII_MODE == gb_datapath_master[lchip].serdes_cfg[serdes_idx].mode )
                {
                    div = 4;
                }
                else
                {
                    div = 1;
                }

                if ((port_cap->mac_id / div) == (gb_datapath_master[lchip].serdes_cfg[serdes_idx].mac_id / div))
                {
                    port_cap->serdes_id  = serdes_idx;
                    port_cap->pcs_mode   = gb_datapath_master[lchip].serdes_cfg[serdes_idx].mode;
                    port_cap->speed_mode = gb_datapath_master[lchip].serdes_cfg[serdes_idx].speed;
                    port_cap->hss_id     = gb_datapath_master[lchip].serdes_cfg[serdes_idx].hss_id;
                    break;
                }
            }
        }

    }
    else
    {
        port_cap->chan_id = INVALID_CHAN_ID;
        port_cap->mac_id  = INVALID_CHAN_ID;
    }

    return port_cap->chan_id;
}

int32
drv_greatbelt_set_port_capability(uint8 lchip, uint8 lport, drv_datapath_port_capability_t port_cap)
{
    gb_datapath_master[lchip].port_capability[lport].chan_id    = port_cap.chan_id;
    gb_datapath_master[lchip].port_capability[lport].mac_id     = port_cap.mac_id;
    gb_datapath_master[lchip].port_capability[lport].serdes_id  = port_cap.serdes_id;
    gb_datapath_master[lchip].port_capability[lport].pcs_mode   = port_cap.pcs_mode;
    gb_datapath_master[lchip].port_capability[lport].speed_mode = port_cap.speed_mode;
    gb_datapath_master[lchip].port_capability[lport].hss_id     = port_cap.hss_id;
    gb_datapath_master[lchip].port_capability[lport].port_type  = port_cap.port_type;
    gb_datapath_master[lchip].port_capability[lport].valid      = port_cap.valid;

    return 0;
}

uint8
drv_greatbelt_get_ignore_mode(uint8 lchip)
{
    if(gb_datapath_master[lchip].ignore_mode)
    {
        return 1;
    }

    return 0;
}

int32
drv_greatbelt_get_port_with_serdes(uint8 lchip, uint16 serdes_id, uint16* p_lport)
{
    uint16 index = 0;
    drv_datapath_port_capability_t port_cap;

    if (serdes_id >= HSS_SERDES_LANE_NUM)
    {
        return DRV_E_EXCEED_MAX_SIZE;
    }

    for (index = 0; index < MAX_LOCAL_PORT_NUM; index++)
    {
        sal_memset(&port_cap, 0, sizeof(drv_datapath_port_capability_t));
        drv_greatbelt_get_port_capability(lchip, index, &port_cap);
        if (port_cap.valid && (port_cap.pcs_mode != DRV_SERDES_NONE_MODE))
        {
            if (port_cap.serdes_id == serdes_id)
            {
                *p_lport = index;
                return 0;
            }
        }
    }

    return DRV_E_HSS_LANE_IS_NOT_USED;
}

uint8
drv_greatbelt_get_port_attr(uint8 lchip, uint8 lport, drv_datapath_port_capability_t** port_cap)
{

    *port_cap = &gb_datapath_master[lchip].port_capability[lport];

    return 0;
}
