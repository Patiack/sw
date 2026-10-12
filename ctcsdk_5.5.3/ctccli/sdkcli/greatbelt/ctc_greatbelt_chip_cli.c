/**
 @file ctc_greatbelt_chip_cli.c

 @date 2012-3-20

 @version v2.0

---file comments----
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_chip.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_greatbelt_chip_cli.h"
#include "sys_greatbelt_chip.h"

extern int32
sys_greatbelt_chip_show_datapath (uint8 lchip, sys_datapath_debug_type_t type);
extern int32
sys_greatbelt_chip_show_status(uint8 lchip);

CTC_CLI(ctc_cli_chip_write_gephy_interface,
        ctc_cli_chip_write_gephy_interface_cmd,
        "chip write gephy port GPHYPORT_ID reg REG val VAL",
        "chip module",
        "write operate",
        "ge phy",
        "index of port",
        CTC_CLI_GPHYPORT_ID_DESC,
        "phy register",
        "<0~31>",
        "value",
        "reg value")
{
    int32 ret       = CLI_SUCCESS;
    ctc_chip_gephy_para_t gephy_para;
    uint16 gport = 0;

    sal_memset(&gephy_para, 0, sizeof(ctc_chip_gephy_para_t));

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);
    CTC_CLI_GET_UINT8_RANGE("phy register", gephy_para.reg, argv[1], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT16_RANGE("reg value", gephy_para.val, argv[2], 0, CTC_MAX_UINT16_VALUE);

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_write_gephy_reg(g_api_lchip, gport, &gephy_para);
    }
    else
    {
        ret = ctc_chip_write_gephy_reg(gport, &gephy_para);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_chip_read_gephy_interface,
        ctc_cli_chip_read_gephy_interface_cmd,
        "chip read gephy port GPHYPORT_ID reg REG",
        "chip module",
        "read operate",
        "ge phy",
        "index of port",
        CTC_CLI_GPHYPORT_ID_DESC,
        "phy register",
        "<0~31>")
{
    int32 ret       = CLI_SUCCESS;
    ctc_chip_gephy_para_t gephy_para;
    uint16 gport = 0;

    sal_memset(&gephy_para, 0, sizeof(ctc_chip_gephy_para_t));

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);
    CTC_CLI_GET_UINT8_RANGE("phy register", gephy_para.reg, argv[1], 0, CTC_MAX_UINT8_VALUE);

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_read_gephy_reg(g_api_lchip, gport, &gephy_para);
    }
    else
    {
        ret = ctc_chip_read_gephy_reg(gport, &gephy_para);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out(" value = 0x%08x \n", gephy_para.val);

    return ret;

}


CTC_CLI(ctc_cli_chip_write_xgphy_interface,
        ctc_cli_chip_write_xgphy_interface_cmd,
        "chip write xgphy port GPHYPORT_ID dev DEV_NO reg REG val VAL",
        "chip module",
        "write operate",
        "xg phy",
        "index of port",
        CTC_CLI_GPHYPORT_ID_DESC,
        "device eg:pmd/wis/phy xgxs/dte xgxs",
        "device no",
        "phy register",
        "<0~31>",
        "value",
        "reg value")
{
    int32 ret       = CLI_SUCCESS;
    ctc_chip_xgphy_para_t xgphy_para;
    uint16 gport = 0;

    sal_memset(&xgphy_para, 0, sizeof(ctc_chip_xgphy_para_t));

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);
    CTC_CLI_GET_UINT8_RANGE("device no", xgphy_para.devno, argv[1], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT8_RANGE("phy register", xgphy_para.reg, argv[2], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT16_RANGE("reg value", xgphy_para.val, argv[3], 0, CTC_MAX_UINT16_VALUE);

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_write_xgphy_reg(g_api_lchip, gport, &xgphy_para);
    }
    else
    {
        ret = ctc_chip_write_xgphy_reg(gport, &xgphy_para);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_chip_read_xgphy_interface,
        ctc_cli_chip_read_xgphy_interface_cmd,
        "chip read xgphy port GPHYPORT_ID dev DEV_NO reg REG",
        "chip module",
        "read operate",
        "xg phy",
        "index of port",
        "<1~52>",
        "device eg:pmd/wis/phy xgxs/dte xgxs",
        "device no",
        "phy register",
        "<0~31>")
{
    int32 ret       = CLI_SUCCESS;
    ctc_chip_xgphy_para_t xgphy_para;
    uint16 gport = 0;

    sal_memset(&xgphy_para, 0, sizeof(ctc_chip_xgphy_para_t));

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);
    CTC_CLI_GET_UINT8_RANGE("device no", xgphy_para.devno, argv[1], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT8_RANGE("phy register", xgphy_para.reg, argv[2], 0, CTC_MAX_UINT8_VALUE);

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_read_xgphy_reg(g_api_lchip, gport, &xgphy_para);
    }
    else
    {
        ret = ctc_chip_read_xgphy_reg(gport, &xgphy_para);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out(" Reg = 0x%08x \n", xgphy_para.val);

    return ret;

}


CTC_CLI(ctc_cli_chip_set_gephy_scan_special_reg,
        ctc_cli_chip_set_gephy_scan_special_reg_cmd,
        "chip set phy-scan special-reg gephy reg REG ((enable bit BIT_CTL intr (enable | disable))|disable)",
        "chip module",
        "set operate",
        "auto scan operate",
        "special reg",
        "ge phy",
        "phy register",
        "<0~31>",
        "enable",
        "bit control",
        "<0~15>",
        "interrupt",
        "interrupt enable",
        "interrupt disable",
        "scan disable")
{
    int32 ret       = CLI_SUCCESS;
    ctc_chip_ge_opt_reg_t scan_reg;
    bool enable;
    uint8 index = 0;

    sal_memset(&scan_reg, 0, sizeof(ctc_chip_ge_opt_reg_t));

    CTC_CLI_GET_UINT8_RANGE("reg", scan_reg.reg, argv[0], 0, CTC_MAX_UINT8_VALUE);

    if (argc > 2)
    {
        enable = TRUE;
    }
    else
    {
        enable = FALSE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("bit");
    if (index != 0xFF)
    {
        if (index + 1 >= argc)
        {
            ctc_cli_out("%% Insufficient  param \n\r");
            return CLI_ERROR;
        }

        CTC_CLI_GET_UINT8_RANGE("bit", scan_reg.bit_ctl, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("intr");
    if (index != 0xFF)
    {
        if (index + 1 >= argc)
        {
            ctc_cli_out("%% Insufficient  param \n\r");
            return CLI_ERROR;
        }

        if (CLI_CLI_STR_EQUAL("enable", index + 1))
        {
            scan_reg.intr_enable = 1;
        }
        else
        {
            scan_reg.intr_enable = 0;
        }
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_set_gephy_scan_special_reg(g_api_lchip, &scan_reg, enable);
    }
    else
    {
        ret = ctc_chip_set_gephy_scan_special_reg(&scan_reg, enable);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}


CTC_CLI(ctc_cli_chip_set_xgphy_scan_special_reg,
        ctc_cli_chip_set_xgphy_scan_special_reg_cmd,
        "chip set phy-scan special-reg xgphy dev DEV reg REG ((enable bit BIT_CTL intr (enable | disable))|disable)",
        "chip module",
        "set operate",
        "auto scan operate",
        "special reg",
        "xg phy",
        "device type",
        "<1~5>",
        "phy register",
        "<0~31>",
        "enable",
        "bit control",
        "<0~15>",
        "interrupt",
        "interrupt enable",
        "interrupt disable",
        "scan disable")
{
    int32 ret       = CLI_SUCCESS;
    ctc_chip_xg_opt_reg_t scan_reg;
    bool enable;
    uint8 index = 0;

    sal_memset(&scan_reg, 0, sizeof(scan_reg));

    CTC_CLI_GET_UINT8_RANGE("dev", scan_reg.dev, argv[0], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT8_RANGE("reg", scan_reg.reg, argv[1], 0, CTC_MAX_UINT8_VALUE);

    if (argc > 3)
    {
        enable = TRUE;
    }
    else
    {
        enable = FALSE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("bit");
    if (index != 0xFF)
    {
        if (index + 1 >= argc)
        {
            ctc_cli_out("%% Insufficient  param \n\r");
            return CLI_ERROR;
        }

        CTC_CLI_GET_UINT8_RANGE("bit", scan_reg.bit_ctl, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("intr");
    if (index != 0xFF)
    {
        if (index + 1 >= argc)
        {
            ctc_cli_out("%% Insufficient  param \n\r");
            return CLI_ERROR;
        }

        if (CLI_CLI_STR_EQUAL("enable", index + 1))
        {
            scan_reg.intr_enable = 1;
        }
        else
        {
            scan_reg.intr_enable = 0;
        }
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_set_xgphy_scan_special_reg(g_api_lchip, &scan_reg, enable);
    }
    else
    {
        ret = ctc_chip_set_xgphy_scan_special_reg(&scan_reg, enable);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}


CTC_CLI(ctc_cli_chip_show_datapath_infor,
        ctc_cli_chip_show_datapath_infor_cmd,
        "show chip datapath information (pll|hss|serdes|mac|synce|calender|misc|channel|inter-port) (lchip LCHIP|)",
        "Chip module",
        CTC_CLI_SHOW_STR,
        "Datapath",
        "Information",
        "Pll",
        "Hss",
        "SerDes",
        "Mac",
        "Synce",
        "Calender",
        "Misc",
        "Channel",
        "Internal port",
        "Local chip id",
        "Chip ID",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    sys_datapath_debug_type_t type = SYS_CHIP_HSS_MAX_DEBUG_TYPE;
    uint8 index = 0;
    uint8 lchip = 0;

    if (0 == sal_memcmp("pll", argv[0], 3))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_PLL;
    }
    else if (0 == sal_memcmp("hss", argv[0], 3))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_HSS;
    }
    else if (0 == sal_memcmp("serdes", argv[0], 5))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_SERDES;
    }
    else if (0 == sal_memcmp("mac", argv[0], 3))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_MAC;
    }
    else if (0 == sal_memcmp("synce", argv[0], 5))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_SYNCE;
    }
    else if (0 == sal_memcmp("calender", argv[0], 3))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_CALDE;
    }
    else if (0 == sal_memcmp("misc", argv[0], 4))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_MISC;
    }
    else if (0 == sal_memcmp("channel", argv[0], 4))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_CHAN;
    }
    else if (0 == sal_memcmp("inter-port", argv[0], 4))
    {
        type = SYS_CHIP_DATAPATH_DEBUG_INTER_PORT;
    }
    else
    {
        type = SYS_CHIP_HSS_MAX_DEBUG_TYPE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if(0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip id", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_chip_show_datapath(lchip, type);
    if (ret != CTC_E_NONE)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_chip_set_hss12g_enable,
        ctc_cli_gb_chip_set_hss12g_enable_cmd,
        "chip set hss12g HSS_ID (xfi|sgmii|qsgmii|disable) (lchip LCHIP|)",
        "chip module",
        "set operate",
        "hss12g macro",
        "hss id",
        "xfi mode",
        "sgmii mode",
        "qsgmii mode",
        "disable hss12g",
        "local chip",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret       = CLI_SUCCESS;
    uint8 hss_id = 0;
    uint8 enable = 1;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_chip_serdes_mode_t mode = CTC_CHIP_SERDES_SGMII_MODE;

    CTC_CLI_GET_UINT8_RANGE("hss_id", hss_id, argv[0], 0, CTC_MAX_UINT8_VALUE);

    if (0 == sal_memcmp("xfi", argv[1], 3))
    {
        mode = CTC_CHIP_SERDES_XFI_MODE;
    }
    else if (0 == sal_memcmp("sgmii", argv[1], 3))
    {
        mode = CTC_CHIP_SERDES_SGMII_MODE;
    }
    else if (0 == sal_memcmp("qsgmii", argv[1], 3))
    {
        mode = CTC_CHIP_SERDES_QSGMII_MODE;
    }
    else
    {
        enable = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (index != 0xFF)
    {
        if (index + 1 >= argc)
        {
            ctc_cli_out("%% Insufficient  param \n\r");
            return CLI_ERROR;
        }

        CTC_CLI_GET_UINT8_RANGE("lchip", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);

    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_chip_set_hss12g_enable(g_api_lchip, hss_id, mode, enable);
    }
    else
    {
        ret = ctc_chip_set_hss12g_enable(lchip, hss_id, mode, enable);
    }
    if (ret != CTC_E_NONE)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gb_chip_show_chip_info,
        ctc_cli_gb_chip_show_chip_info_cmd,
        "show chip status (lchip LCHIP|)",
        "show",
        "chip module",
        "chip information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0;
    uint8 index = 0;
    int32 ret = CLI_SUCCESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_chip_show_status(lchip);
    if (ret)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_greatbelt_chip_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_chip_write_gephy_interface_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_chip_read_gephy_interface_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_chip_write_xgphy_interface_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_chip_read_xgphy_interface_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_chip_set_gephy_scan_special_reg_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_chip_set_xgphy_scan_special_reg_cmd);

    install_element(CTC_SDK_MODE, &ctc_cli_chip_show_datapath_infor_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_chip_set_hss12g_enable_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_chip_show_chip_info_cmd);

    return 0;
}

int32
ctc_greatbelt_chip_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_write_gephy_interface_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_read_gephy_interface_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_write_xgphy_interface_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_read_xgphy_interface_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_set_gephy_scan_special_reg_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_set_xgphy_scan_special_reg_cmd);

    uninstall_element(CTC_SDK_MODE, &ctc_cli_chip_show_datapath_infor_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_chip_set_hss12g_enable_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_chip_show_chip_info_cmd);

    return 0;
}

