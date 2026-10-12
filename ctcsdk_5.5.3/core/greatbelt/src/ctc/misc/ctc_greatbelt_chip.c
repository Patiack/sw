/**
 @file ctc_greatbelt_chip.c

 @author  Copyright (C) 2011 Centec Networks Inc.  All rights reserved.

 @date 2009-10-19

 @version v2.0

 The file contains all chip APIs of ctc layer
*/

/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_const.h"
#include "ctc_greatbelt_chip.h"
#include "sys_greatbelt_chip.h"
/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/

/**
 @brief Initialize the chip module and set the local chip number of the linecard
*/
int32
ctc_greatbelt_chip_init(uint8 lchip, uint8 lchip_num)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = lchip_num;
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 0)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_init(lchip, lchip_num));
    }

    return CTC_E_NONE;
}

int32
ctc_greatbelt_chip_deinit(uint8 lchip)
{
    uint8 lchip_start       = 0;
    uint8 lchip_end         = 0;

    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_deinit(lchip));
    }

    return CTC_E_NONE;
}

/**
 @brief The function is to parse datapath configure file
*/
int32
ctc_greatbelt_parse_datapath(uint8 lchip_id, char* datapath_config_file)
{
    uint8 lchip = 0;
    lchip = lchip_id;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_parse_datapath_file(lchip, datapath_config_file));

    return CTC_E_NONE;
}

/**
 @brief The function is to init pll and hss
*/
int32
ctc_greatbelt_init_pll_hss(uint8 lchip_id)
{
    uint8 lchip = 0;
    lchip = lchip_id;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_init_pll_hss(lchip));

    return CTC_E_NONE;
}

/**
 @brief The function is to init datapath
*/
int32
ctc_greatbelt_datapath_init(uint8 lchip_id, void* p_global_cfg)
{
    uint8 lchip = lchip_id;
    char datapath_file[50] = {0};

    LCHIP_CHECK(lchip);
    if (0 == SDK_WORK_PLATFORM)
    {
        if ((NULL == p_global_cfg) || (NULL == ((ctc_datapath_global_cfg_t*)p_global_cfg)->file_name))
        {
            if (lchip_id == 0)
            {
                sal_strcpy(datapath_file, "datapath_cfg.txt");
            }
            else if (lchip_id == 1)
            {
                sal_strcpy(datapath_file, "datapath_cfg1.txt");
            }
        }
        else
        {
            sal_strcpy(datapath_file, ((ctc_datapath_global_cfg_t*)p_global_cfg)->file_name);
        }

        CTC_ERROR_RETURN(sys_greatbelt_parse_datapath_file(lchip, datapath_file));
        CTC_ERROR_RETURN(sys_greatbelt_datapath_init(lchip));
    }
    else
    {
        CTC_ERROR_RETURN(sys_greatbelt_datapath_sim_init(lchip));
    }
    return CTC_E_NONE;
}

/**
 @brief The function is to init datapath
*/
int32
ctc_greatbelt_datapath_sim_init(uint8 lchip_id)
{
    uint8 lchip = 0;
    lchip = lchip_id;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_datapath_sim_init(lchip));

    return CTC_E_NONE;
}

/**
 @brief The function is to deinit datapath
*/
int32
ctc_greatbelt_datapath_deinit(uint8 lchip)
{
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_datapath_deinit(lchip));

    return CTC_E_NONE;
}

/**
 @brief The function is to get the local chip number of the linecard
*/
int32
ctc_greatbelt_get_local_chip_num(uint8 lchip, uint8* lchip_num)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_PTR_VALID_CHECK(lchip_num);
    *lchip_num = sys_greatbelt_get_local_chip_num(lchip);

    return CTC_E_NONE;
}

/**
 @brief The function is to set chip's global chip id
*/
int32
ctc_greatbelt_set_gchip_id(uint8 lchip, uint8 gchip_id)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_set_gchip_id(lchip, gchip_id));

    return CTC_E_NONE;
}

/**
 @brief The function is to get chip's global chip id
*/
int32
ctc_greatbelt_get_gchip_id(uint8 lchip, uint8* gchip_id)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_ERROR_RETURN(sys_greatbelt_get_gchip_id(lchip, gchip_id));

    return CTC_E_NONE;
}

/**
 @brief The function is to set chip's global cnfig
*/
int32
ctc_greatbelt_set_chip_global_cfg(uint8 lchip, ctc_chip_global_cfg_t* chip_cfg)
{
    uint8 index = 0;
    ctc_chip_global_cfg_t   chip_global_cfg;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip, CTC_FEATURE_CHIP);
    sal_memset(&chip_global_cfg,0,sizeof(ctc_chip_global_cfg_t));
    CTC_SET_API_LCHIP(lchip, chip_cfg->lchip);


        if ( NULL == chip_cfg )
        {
            sal_memset(&chip_global_cfg, 0, sizeof(ctc_chip_global_cfg_t));
            chip_global_cfg.lchip = lchip;
            chip_global_cfg.cpu_mac_sa[0] = 0xFE;
            chip_global_cfg.cpu_mac_sa[1] = 0xFD;
            chip_global_cfg.cpu_mac_sa[2] = 0x0;
            chip_global_cfg.cpu_mac_sa[3] = 0x0;
            chip_global_cfg.cpu_mac_sa[4] = 0x0;
            chip_global_cfg.cpu_mac_sa[5] = 0x1;

            /*init CPU MAC DA*/
            for (index = 0; index < MAX_CTC_CPU_MACDA_NUM; index++)
            {
                chip_global_cfg.cpu_mac_da[index][0] = 0xFE;
                chip_global_cfg.cpu_mac_da[index][1] = 0xFD;
                chip_global_cfg.cpu_mac_da[index][2] = 0x0;
                chip_global_cfg.cpu_mac_da[index][3] = 0x0;
                chip_global_cfg.cpu_mac_da[index][4] = 0x0;
                chip_global_cfg.cpu_mac_da[index][5] = 0x0;
            }
            chip_global_cfg.tpid                = 0x8100;
            chip_global_cfg.vlanid              = 0x23;
            chip_global_cfg.cut_through_en      = 0;
            chip_global_cfg.cut_through_speed   = 0;
            chip_global_cfg.ecc_recover_en      = 0;
            chip_global_cfg.cpu_port_en = 0;
            chip_cfg = &chip_global_cfg;
        }
        else
        {
            chip_cfg->lchip = lchip;
        }

        CTC_ERROR_RETURN(sys_greatbelt_set_chip_global_cfg(lchip, chip_cfg));

        if (chip_cfg->ecc_recover_en)
        {
            CTC_ERROR_RETURN(sys_greatbelt_chip_ecc_recover_init(lchip));
        }


    return CTC_E_NONE;
}

/**
 @brief The function is to get chip's frequency
*/
int32
ctc_greatbelt_get_chip_clock(uint8 lchip, uint16* freq)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_PTR_VALID_CHECK(freq);
    CTC_ERROR_RETURN(sys_greatbelt_get_chip_clock(lchip, freq));

    return CTC_E_NONE;
}

/**
 @brief Set port to mdio and port to phy mapping
*/
int32
ctc_greatbelt_chip_set_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_ERROR_RETURN(sys_greatbelt_chip_set_phy_mapping(lchip, phy_mapping_para));

    return CTC_E_NONE;
}

/**
 @brief Get port to mdio and port to phy mapping
*/
int32
ctc_greatbelt_chip_get_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_get_phy_mapping(lchip, phy_mapping_para));

    return CTC_E_NONE;
}

/**
 @brief Write Gephy register value
*/
int32
ctc_greatbelt_chip_write_gephy_reg(uint8 lchip, uint32 gport, ctc_chip_gephy_para_t* gephy_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_write_gephy_reg(lchip, gport, gephy_para));
    return CTC_E_NONE;
}

/**
 @brief Read Gephy register value
*/
int32
ctc_greatbelt_chip_read_gephy_reg(uint8 lchip, uint32 gport, ctc_chip_gephy_para_t* gephy_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_read_gephy_reg(lchip, gport, gephy_para));
    return CTC_E_NONE;
}

/**
 @brief Write XGphy register value
*/
int32
ctc_greatbelt_chip_write_xgphy_reg(uint8 lchip, uint32 gport, ctc_chip_xgphy_para_t* xgphy_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_write_xgphy_reg(lchip, gport, xgphy_para));
    return CTC_E_NONE;
}

/**
 @brief Read XGphy register value
*/
int32
ctc_greatbelt_chip_read_xgphy_reg(uint8 lchip, uint32 gport, ctc_chip_xgphy_para_t* xgphy_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    SYS_MAP_GPORT_TO_LCHIP(gport, lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_read_xgphy_reg(lchip, gport, xgphy_para));
    return CTC_E_NONE;
}

/**
 @brief set gephy scan optional reg
*/
int32
ctc_greatbelt_chip_set_gephy_scan_special_reg(uint8 lchip, ctc_chip_ge_opt_reg_t* p_scan_opt, bool enable)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, p_scan_opt->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_gephy_scan_special_reg(lchip, p_scan_opt, enable));
    return CTC_E_NONE;
}

/**
 @brief set xgphy scan optional reg
*/
int32
ctc_greatbelt_chip_set_xgphy_scan_special_reg(uint8 lchip, ctc_chip_xg_opt_reg_t* scan_opt, uint8 enable)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, scan_opt->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_xgphy_scan_special_reg(lchip, scan_opt, enable));
    return CTC_E_NONE;
}

/**
 @brief control gephy scan
*/
int32
ctc_greatbelt_chip_set_phy_scan_para(uint8 lchip, ctc_chip_phy_scan_ctrl_t* p_scan_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_PTR_VALID_CHECK(p_scan_para);
    CTC_SET_API_LCHIP(lchip, p_scan_para->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_phy_scan_para(lchip, p_scan_para));
    return CTC_E_NONE;
}

/**
 @brief read phy scan enablle
*/
int32
ctc_greatbelt_chip_set_phy_scan_en(uint8 lchip, bool enable)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
         CTC_ERROR_RETURN(sys_greatbelt_chip_set_phy_scan_en(lchip, enable));
    }
    return CTC_E_NONE;
}

/**
 @brief read by i2c master interface
*/
int32
ctc_greatbelt_chip_i2c_read(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para)
{
    CTC_PTR_VALID_CHECK(p_i2c_para);
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);

    CTC_SET_API_LCHIP(lchip, p_i2c_para->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_i2c_read(lchip, p_i2c_para));
    return CTC_E_NONE;
}

/**
 @brief write sfp register by i2c master interface
*/
int32
ctc_greatbelt_chip_i2c_write(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para)
{
    CTC_PTR_VALID_CHECK(p_i2c_para);
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, p_i2c_para->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_i2c_write(lchip, p_i2c_para));
    return CTC_E_NONE;
}

/**
 @brief set sfp scan parameter
*/
int32
ctc_greatbelt_chip_set_i2c_scan_para(uint8 lchip, ctc_chip_i2c_scan_t* p_i2c_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, p_i2c_para->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_i2c_scan_para(lchip, p_i2c_para));
    return CTC_E_NONE;
}

/**
 @brief set sfp scan enable
*/
int32
ctc_greatbelt_chip_set_i2c_scan_en(uint8 lchip_id, bool enable)
{
    uint8 lchip = lchip_id;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_i2c_scan_en(lchip, enable));
    return CTC_E_NONE;
}

/**
 @brief set read i2c buffer interface
*/
int32
ctc_greatbelt_chip_read_i2c_buf(uint8 lchip, ctc_chip_i2c_scan_read_t *p_i2c_scan_read)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, p_i2c_scan_read->gchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_read_i2c_buf(lchip, p_i2c_scan_read));
    return CTC_E_NONE;
}

/**
 @brief set mac led mode interface
*/
int32
ctc_greatbelt_chip_set_mac_led_mode(uint8 lchip, ctc_chip_led_para_t* p_led_para, ctc_chip_mac_led_type_t led_type)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, p_led_para->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_mac_led_mode(lchip, p_led_para, led_type, 0));
    return CTC_E_NONE;
}

/**
 @brief set mac led mapping
*/
int32
ctc_greatbelt_chip_set_mac_led_mapping(uint8 lchip, ctc_chip_mac_led_mapping_t* p_led_map)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_SET_API_LCHIP(lchip, p_led_map->lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_mac_led_mapping(lchip, p_led_map));
    return CTC_E_NONE;
}

/**
 @brief set mac led enable
*/
int32
ctc_greatbelt_chip_set_mac_led_en(uint8 lchip, bool enable)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_mac_led_en(lchip, enable));
    }
    return CTC_E_NONE;
}

/**
 @brief set gpio interface
*/
int32
ctc_greatbelt_chip_set_gpio_mode(uint8 lchip, uint8 gpio_id, ctc_chip_gpio_mode_t mode)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_gpio_mode(lchip, gpio_id, mode));
    }
    return CTC_E_NONE;
}

/**
 @brief set gpio output value
*/
int32
ctc_greatbelt_chip_set_gpio_output(uint8 lchip, uint8 gpio, uint8 out_para)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_gpio_output(lchip, gpio, out_para));
    }
    return CTC_E_NONE;
}

/**
 @brief switch chip access type
*/
int32
ctc_greatbelt_chip_set_access_type(uint8 lchip, ctc_chip_access_type_t access_type)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_access_type(lchip, access_type));
    }
    return CTC_E_NONE;
}

/**
 @brief get chip access type
*/
int32
ctc_greatbelt_chip_get_access_type(uint8 lchip, ctc_chip_access_type_t* p_access_type)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_get_access_type(lchip, p_access_type));
    }
    return CTC_E_NONE;
}

/**
 @brief set chip dynamic switch mode
*/
int32
ctc_greatbelt_chip_set_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_serdes_mode(lchip, p_serdes_info));
    return CTC_E_NONE;
}

/**
 @brief Mdio read interface
*/
int32
ctc_greatbelt_chip_mdio_read(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_mdio_read(lchip, type, p_para));

    return CTC_E_NONE;
}

/**
 @brief Mdio read interface
*/
int32
ctc_greatbelt_chip_mdio_write(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_mdio_write(lchip, type, p_para));

    return CTC_E_NONE;
}

/**
 @brief reset hss12g macro
*/
int32
ctc_greatbelt_chip_set_hss12g_enable(uint8 lchip_id, uint8 macro_id, ctc_chip_serdes_mode_t mode, uint8 enable)
{
    uint8 lchip = lchip_id;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_hss12g_enable(lchip, macro_id,mode, enable));

    return CTC_E_NONE;
}

/**
 @brief set mdio clock frequency
*/
int32
ctc_greatbelt_chip_set_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16 freq)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_set_mdio_clock(lchip, type, freq));
    }

    return CTC_E_NONE;
}

/**
 @brief get mdio clock frequency
*/
int32
ctc_greatbelt_chip_get_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16* p_freq)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_greatbelt_chip_get_mdio_clock(lchip, type, p_freq));
    }

    return CTC_E_NONE;
}

/**
 @brief get sensor value
*/
int32
ctc_greatbelt_get_chip_sensor(uint8 lchip, ctc_chip_sensor_type_t type, uint32* p_value)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_get_chip_sensor(lchip, type, p_value));

    return CTC_E_NONE;
}

/**
 @brief set chip property
*/
int32
ctc_greatbelt_chip_set_property(uint8 lchip,  ctc_chip_property_t chip_prop,  void* p_value)
{
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_greatbelt_chip_set_property(lchip, chip_prop, p_value));
    return CTC_E_NONE;
}

/**
 @brief get chip property
*/
int32
ctc_greatbelt_chip_get_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value)
{
    LCHIP_CHECK(lchip);
    FEATURE_SUPPORT_CHECK(lchip,CTC_FEATURE_CHIP);
    CTC_ERROR_RETURN(sys_greatbelt_chip_get_property(lchip, chip_prop, p_value));
    return CTC_E_NONE;
}

