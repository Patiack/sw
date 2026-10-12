/**
 @file ctc_goldengate_stats_cli.c

 @date 2010-06-09

 @version v2.0

 This file defines functions for stats internal cli module

*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_error.h"
#include "ctc_cli.h"
#include "ctc_const.h"
#include "ctc_cli_common.h"
#include "ctc_api.h"
#include "ctcs_api.h"

extern int32
sys_goldengate_stats_show_status(uint8 lchip);



CTC_CLI(ctc_cli_gg_stats_show_status,
        ctc_cli_gg_stats_show_status_cmd,
        "show stats status (lchip LCHIP|)",
        "Show",
        "Stats Module",
        "Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret= CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
    	CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_stats_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

/**
 @brief  Initialize sdk stats internal module CLIs
*/
int32
ctc_goldengate_stats_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_stats_show_status_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_stats_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_stats_show_status_cmd);

    return CLI_SUCCESS;
}

