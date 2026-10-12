/**
 @file ctc_greatbelt_aps_cli.c

 @date 2012-12-6

 @version v2.0

 The file apply clis of ipuc module
*/

#include "sal.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_aps_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_aps.h"

extern int32
sys_greatbelt_aps_show_bridge(uint8 lchip, uint16 group_id);

CTC_CLI(ctc_cli_gb_aps_show_bridge,
        ctc_cli_gb_aps_show_bridge_cmd,
        "show aps bridge group GROUP_ID (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_APS_M_STR,
        "APS Bridge",
        "APS bridge group",
        CTC_CLI_APS_GROUP_ID_DESC,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0;
    uint16 group_id = 0;

    CTC_CLI_GET_UINT16_RANGE("aps bridge group id", group_id, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_aps_show_bridge(lchip, group_id);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_greatbelt_aps_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_aps_show_bridge_cmd);
    return CLI_SUCCESS;
}

int32
ctc_greatbelt_aps_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_aps_show_bridge_cmd);
    return CLI_SUCCESS;
}

