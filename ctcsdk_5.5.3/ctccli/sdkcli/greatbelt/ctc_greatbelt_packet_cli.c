/**
 @file ctc_greatbelt_packet_cli.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-11-25

 @version v2.0

 This file define packet CLI functions

*/

#include "sal.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_packet.h"
#include "ctc_greatbelt_packet.h"
#include "ctc_greatbelt_packet_cli.h"


extern int32
sys_greatbelt_packet_dump(uint8 lchip);
extern int32
sys_greatbelt_packet_stats_dump(uint8 lchip);
extern int32
sys_greatbelt_packet_stats_clear(uint8 lchip);

extern int32
sys_greatbelt_packet_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag);
extern int32
sys_greatbelt_packet_tx_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag);
extern int32
sys_greatbelt_packet_buffer_clear(uint8 lchip ,uint8 bitmap);
extern int32
sys_greatbelt_packet_set_reason_log(uint8 lchip, uint16 reason_id, uint8 enable, uint8 is_all, uint8 is_detail);

extern int32
_ctc_cli_packet_get_from_file(char* file, uint8* buf, uint32* pkt_len);

CTC_CLI(ctc_cli_gb_packet_show_packet_info,
        ctc_cli_gb_packet_show_packet_info_cmd,
        "show packet (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PACKET_M_STR,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_packet_dump(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_packet_show_packet_stats,
        ctc_cli_gb_packet_show_packet_stats_cmd,
        "show packet stats (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PACKET_M_STR,
        "Stats",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_packet_stats_dump(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_packet_clear_packet_stats,
        ctc_cli_gb_packet_clear_packet_stats_cmd,
        "clear packet stats (lchip LCHIP|)",
        CTC_CLI_CLEAR_STR,
        CTC_CLI_PACKET_M_STR,
        "Stats",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_packet_stats_clear(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gb_packet_show_packet_buffer,
        ctc_cli_gb_packet_show_packet_buffer_cmd,
        "show packet (rx|tx) {buffer-count CNT | packet-header | stacking-header|} (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PACKET_M_STR,
        "Rx",
        "Tx",
        "The recent buffer",
        "Buffer count",
        "Packet header info",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 index = 0;
    uint8 buf_cnt = 0;
    uint8 flag = 0;
    uint8 lchip = 0;


    index = CTC_CLI_GET_ARGC_INDEX("buffer-count");
    if (INDEX_VALID(index))
    {
        CTC_CLI_GET_UINT8("buffer-count", buf_cnt, argv[index + 1]);
        CTC_BIT_SET(flag, 0);
    }

    index = CTC_CLI_GET_ARGC_INDEX("packet-header");
    if (INDEX_VALID(index))
    {
        CTC_BIT_SET(flag, 1);
    }

    index = CTC_CLI_GET_ARGC_INDEX("stacking-header");
    if (INDEX_VALID(index))
    {
        CTC_BIT_SET(flag, 2);
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("rx");
    if (INDEX_VALID(index))
    {
        ret = sys_greatbelt_packet_buffer_dump(lchip, buf_cnt, flag);
    }
    else
    {
        ret = sys_greatbelt_packet_tx_buffer_dump(lchip, buf_cnt, flag);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_packet_clear_packet_buffer,
        ctc_cli_gb_packet_clear_packet_buffer_cmd,
        "clear packet {rx|tx} (lchip LCHIP|)",
        CTC_CLI_CLEAR_STR,
        CTC_CLI_PACKET_M_STR,
        "Rx",
        "Tx",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 bitmap = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("rx");
    if (INDEX_VALID(index))
    {
        CTC_BIT_SET(bitmap, 0);
    }

    index = CTC_CLI_GET_ARGC_INDEX("tx");
    if (INDEX_VALID(index))
    {
        CTC_BIT_SET(bitmap, 1);
    }

    ret = sys_greatbelt_packet_buffer_clear(lchip ,bitmap);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_packet_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_packet_show_packet_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_packet_show_packet_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_packet_clear_packet_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_packet_show_packet_buffer_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_packet_clear_packet_buffer_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_packet_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_packet_show_packet_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_packet_show_packet_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_packet_clear_packet_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_packet_show_packet_buffer_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_packet_clear_packet_buffer_cmd);

    return CLI_SUCCESS;
}

