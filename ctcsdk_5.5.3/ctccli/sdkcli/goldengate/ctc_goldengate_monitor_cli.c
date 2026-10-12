/**
 @file ctc_ipfix_cli.c

 @date 2010-06-09

 @version v2.0

 This file defines functions for ptp cli module

*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_monitor.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"

extern int32
sys_goldengate_monitor_show_status(uint8 lchip);

extern int32
sys_goldengate_monitor_show_stats(uint8 lchip, uint16 gport);

extern int32
sys_goldengate_monitor_clear_stats(uint8 lchip, uint16 gport);


CTC_CLI(ctc_cli_gg_monitor_show_status,
        ctc_cli_gg_monitor_show_status_cmd,
        "show monitor status (lchip LCHIP|)",
        "Show",
        "Monitor Module",
        "Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_monitor_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}



CTC_CLI(ctc_cli_gg_monitor_show_stats,
        ctc_cli_gg_monitor_show_stats_cmd,
        "show monitor port GPORT_ID stats (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_MONITOR_M_STR,
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPHYPORT_ID_DESC,
        "stats"
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint16 gport = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT16("port", gport, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_monitor_show_stats(lchip, gport);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_monitor_clear_stats,
        ctc_cli_gg_monitor_clear_stats_cmd,
        "clear monitor port GPORT_ID stats (lchip LCHIP|)",
        "Clear",
        CTC_CLI_MONITOR_M_STR,
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPHYPORT_ID_DESC,
        "stats"
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint16 gport = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT16("port", gport, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_monitor_clear_stats(lchip, gport);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_monitor_set_latency_log_level,
        ctc_cli_gg_monitor_set_latency_log_level_cmd,
        "monitor latency (port GPORT_ID) (log-level LEVEL)",
        CTC_CLI_MONITOR_M_STR,
        "Latency Monitor",
        "Global physical port",
        CTC_CLI_GPHYPORT_ID_DESC,
        "Log level",
        "Value <0-7>")
{
    int32 ret = 0;
    uint8 index = 0;
    uint16 gport = 0;
    uint32 level = 0;
    ctc_monitor_config_t config;
    sal_memset(&config, 0, sizeof(config));

    config.monitor_type = CTC_MONITOR_LATENCY;

    index = CTC_CLI_GET_ARGC_INDEX("port");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT16("gport", gport, argv[index + 1]);
        config.gport = gport;
    }

    index = CTC_CLI_GET_ARGC_INDEX("log-level");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT16("min", level, argv[index + 1]);
        config.cfg_type = CTC_MONITOR_CONFIG_LOG_THRD_LEVEL;
        config.value = level;
    }
   
    if (g_ctcs_api_en)
    {
         ret = ctcs_monitor_set_config(g_api_lchip, &config);
    }
    else
    {
        ret = ctc_monitor_set_config(&config);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}
/**
 @brief  Initialize sdk monitor module CLIs
*/
int32
ctc_goldengate_monitor_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_set_latency_log_level_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_show_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_show_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_clear_stats_cmd);
    return CLI_SUCCESS;
}

int32
ctc_goldengate_monitor_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_set_latency_log_level_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_show_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_show_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_monitor_clear_stats_cmd);
    return CLI_SUCCESS;
}

