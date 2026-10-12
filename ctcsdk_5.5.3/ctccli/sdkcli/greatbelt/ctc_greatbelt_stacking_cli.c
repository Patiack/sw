/**
 @file ctc_greatbelt_stacking_cli.c

 @date 2010-7-9

 @version v2.0

---file comments----
*/

#include "ctc_cli.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"

#include "ctc_greatbelt_stacking_cli.h"
#include "sys_greatbelt_stacking.h"

extern int32 sys_greatbelt_stacking_show_trunk_info(uint8 lchip, uint8 trunk_id);
extern int32 sys_greatbelt_stacking_mcast_mode(uint8 lchip, uint8 mcast_mode);
extern int32 sys_greatbelt_stacking_all_mcast_bind_en(uint8 lchip, uint8 enable);

extern int32 sys_greatbelt_stacking_port_enable(uint8 lchip, ctc_stacking_trunk_t* p_trunk, uint8 enable);

CTC_CLI(ctc_cli_gb_stacking_show_trunk_info,
        ctc_cli_gb_stacking_show_trunk_info_cmd,
        "show stacking trunk trunk-id TRUNID info (lchip LCHIP|)",
        "Show",
        "Stacking",
        "Trunk",
        "Trunk id",
        "Value <1-63>",
        "Information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 trunk_id = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT8_RANGE("Trunkid", trunk_id, argv[0], 0, CTC_MAX_UINT8_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_stacking_show_trunk_info(lchip, trunk_id);

    if (ret < 0)
    {
        ctc_cli_out("%% ret=%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_stacking_mcast_mode,
        ctc_cli_gb_stacking_mcast_mode_cmd,
        "stacking mcast-mode MODE (lchip LCHIP|)",
        "Stacking",
        "Mcast mode",
        "0: add trunk to mcast group auto; 1: add trunk to mcast group by user",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 mode = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT8("mode", mode, argv[0]);
     index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_stacking_mcast_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret=%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_stacking_mcast_bind_en,
        ctc_cli_gb_stacking_mcast_bind_en_cmd,
        "stacking mcast bind (enable|disable) (lchip LCHIP|)",
        "Stacking",
        "Mcast",
        "Bind default profile",
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 enable = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        enable = 1;
    }


    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_stacking_all_mcast_bind_en(lchip, enable);

    if (ret < 0)
    {
        ctc_cli_out("%% ret=%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}




CTC_CLI(ctc_cli_gb_stacking_port_en,
        ctc_cli_gb_stacking_port_en_cmd,
        "stacking (port GPORT) (enable|disable)  (lchip LCHIP|)",
        "Stacking",
        "Gport",
        CTC_CLI_GPHYPORT_ID_DESC,
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    ctc_stacking_trunk_t trunk;
    uint8 enable = 0;

    sal_memset(&trunk, 0, sizeof(ctc_stacking_trunk_t));

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }


    index = CTC_CLI_GET_ARGC_INDEX("port");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT32("gport", trunk.gport, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        enable = 1;
    }
     ret = sys_greatbelt_stacking_port_enable(lchip, &trunk, enable);

    if (ret < 0)
    {
        ctc_cli_out("%% ret=%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_stacking_cli_init(void)
{
    install_element(CTC_SDK_MODE,  &ctc_cli_gb_stacking_show_trunk_info_cmd);
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_stacking_mcast_mode_cmd);
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_stacking_mcast_bind_en_cmd);
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_stacking_port_en_cmd);

    return CLI_SUCCESS;

}

int32
ctc_greatbelt_stacking_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE,  &ctc_cli_gb_stacking_show_trunk_info_cmd);
    uninstall_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_stacking_mcast_mode_cmd);
    uninstall_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_stacking_port_en_cmd);

    return CLI_SUCCESS;

}

