/**
 @file ctc_goldengate_packet_cli.c

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
#include "ctc_goldengate_packet.h"
#include "ctc_goldengate_nexthop.h"

extern int32
sys_goldengate_packet_stats_dump(uint8 lchip);
extern int32
sys_goldengate_packet_stats_clear(uint8 lchip);

extern int32
sys_goldengate_packet_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag);
extern int32
sys_goldengate_packet_tx_buffer_dump(uint8 lchip, uint8 buf_cnt, uint8 flag);
extern int32
sys_goldengate_packet_buffer_clear(uint8 lchip ,uint8 bitmap);

extern int32
sys_goldengate_packet_buffer_dump_discard_pkt(uint8 lchip, uint8 buf_cnt, uint8 flag);
extern int32
sys_goldengate_packet_buffer_clear_discard_pkt(uint8 lchip);
extern int32
sys_goldengate_packet_tx_discard_pkt(uint8 lchip, uint8 buf_id, ctc_pkt_tx_t* p_pkt_tx);
extern int32
sys_goldengate_packet_set_reason_log(uint8 lchip, uint16 reason_id, uint8 enable, uint8 is_all, uint8 is_detail);

extern int32
_ctc_cli_packet_get_from_file(char* file, uint8* buf, uint32* pkt_len);

CTC_CLI(ctc_cli_gg_packet_show_packet_stats,
        ctc_cli_gg_packet_show_packet_stats_cmd,
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
    ret = sys_goldengate_packet_stats_dump(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_packet_clear_packet_stats,
        ctc_cli_gg_packet_clear_packet_stats_cmd,
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
    ret = sys_goldengate_packet_stats_clear(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gg_packet_show_packet_buffer,
        ctc_cli_gg_packet_show_packet_buffer_cmd,
        "show packet (rx|tx) {buffer-count CNT | packet-header |diag-discard-pkt | stacking-header| raw|} (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PACKET_M_STR,
        "Rx",
        "Tx",
        "The recent buffer",
        "Buffer count",
        "Packet header info",
        "Discard packet for diagnostic",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 index = 0;
    uint8 buf_cnt = 0;
    uint8 flag = 0;
    uint8 lchip = 0;
    uint8 packet_mode = 2;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("rx");
    if (INDEX_VALID(index))
    {
        packet_mode = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("tx");
    if (INDEX_VALID(index))
    {
        packet_mode = 1;
    }

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

    index = CTC_CLI_GET_ARGC_INDEX("raw");
    if (INDEX_VALID(index))
    {
        CTC_BIT_SET(flag, 3);
    }

    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    index = CTC_CLI_GET_ARGC_INDEX("diag-discard-pkt");
    if (0xFF != index)
    {
        if (packet_mode == 0)
        {
            ret = sys_goldengate_packet_buffer_dump_discard_pkt(lchip, buf_cnt, flag);
        }
    }
    else
    {
        if (packet_mode == 0)
        {
            ret = sys_goldengate_packet_buffer_dump(lchip, buf_cnt, flag);
        }
        else
        {
            ret = sys_goldengate_packet_tx_buffer_dump(lchip, buf_cnt, flag);
        }

    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_packet_clear_packet_buffer,
        ctc_cli_gg_packet_clear_packet_buffer_cmd,
        "clear packet {rx|tx} (diag-discard-pkt|) (lchip LCHIP|)",
        CTC_CLI_CLEAR_STR,
        CTC_CLI_PACKET_M_STR,
        "Rx",
        "Tx",
        "Discard packet for diagnostic",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 packet_mode = 2;
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
        packet_mode = 0;
        CTC_BIT_SET(bitmap, 0);
    }

    index = CTC_CLI_GET_ARGC_INDEX("tx");
    if (INDEX_VALID(index))
    {
        packet_mode = 1;
        CTC_BIT_SET(bitmap, 1);

    }

    index = CTC_CLI_GET_ARGC_INDEX("diag-discard-pkt");
    if (0xFF != index)
    {
        if (packet_mode == 1)
        {
            ret = sys_goldengate_packet_buffer_clear_discard_pkt(lchip);
        }
    }
    else
    {
        ret = sys_goldengate_packet_buffer_clear(lchip ,bitmap);

    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_packet_dump_packet_rx,
        ctc_cli_gg_packet_dump_packet_rx_cmd,
        "debug packet rx (on|off) (cpu-reason REASON |) (detail |) (lchip LCHIP|)",
        "Debug",
        CTC_CLI_PACKET_M_STR,
        "Rx",
        "Dump packet due to cpu-reason",
        "Debug on",
        "Debug off",
        "Cpu reason",
        "<0-max> refer to cli show qos cpu-reason",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CTC_E_NONE;
    uint16 reason_id = 0;
    uint8 index = 0;
    uint8 enable = 0;
    uint8 is_all = 1;
    uint8 is_detail = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("cpu-reason");
    if (INDEX_VALID(index))
    {
        CTC_CLI_GET_UINT8("reason-id", reason_id, argv[index + 1]);
        is_all = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("on");
    if (INDEX_VALID(index))
    {
        enable = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("off");
    if (INDEX_VALID(index))
    {
        enable = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (INDEX_VALID(index))
    {
        is_detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_packet_set_reason_log(lchip, reason_id, enable, is_all, is_detail);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_packet_tx_diag_discard_packet,
        ctc_cli_gg_packet_tx_diag_discard_packet_cmd,
        "packet tx diag-discard-pkt (dest-gport GPHYPORT_ID) {ingress |buffer-id ID| bypass|dma} (lchip LCHIP|)",
        CTC_CLI_PACKET_M_STR,
        "Send packet",
        "Discard packet for diagnostic",
        "Destination gport",
        CTC_CLI_GPHYPORT_ID_DESC,
        "Send to ingress port by iloop",
        "Buffer ID",
        "Buffer ID",
        "Bypass nexthop for send to egress port",
        "DMA mode")
{
    int32 ret = CTC_E_NONE;
    uint8 index = 0;
    uint8 lchip = 0;
    uint8 buf_id = 0;
    uint32 flags = 0;
    uint32 mode = 0;
    uint32 dest_gport = 0;
    ctc_pkt_tx_t* p_pkt_tx = NULL;

    mode = CTC_PKT_MODE_ETH;
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("buffer-id");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("buffer-id", buf_id, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("dest-gport");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT32("dest-gport", dest_gport, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("bypass");
    if (0xFF != index)
    {
        flags |= CTC_PKT_FLAG_NH_OFFSET_BYPASS;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dma");
    if (0xFF != index)
    {
        mode = CTC_PKT_MODE_DMA;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ingress");
    if (0xFF != index)
    {
        flags |= CTC_PKT_FLAG_INGRESS_MODE;
    }


    p_pkt_tx = (ctc_pkt_tx_t*)mem_malloc(MEM_CLI_MODULE, sizeof(ctc_pkt_tx_t));
    if (NULL == p_pkt_tx)
    {
        return CTC_E_NO_MEMORY;
    }
    sal_memset(p_pkt_tx, 0, sizeof(ctc_pkt_tx_t));
    p_pkt_tx->tx_info.oper_type = CTC_PKT_OPER_NORMAL;
    p_pkt_tx->tx_info.flags = flags;
    p_pkt_tx->mode = mode;
    p_pkt_tx->tx_info.dest_gport = dest_gport;
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_packet_tx_discard_pkt(lchip, buf_id, p_pkt_tx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        mem_free(p_pkt_tx);
        return CLI_ERROR;
    }
    mem_free(p_pkt_tx);

    return CLI_SUCCESS;
}


int32
ctc_goldengate_packet_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_packet_show_packet_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_packet_clear_packet_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_packet_show_packet_buffer_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_packet_clear_packet_buffer_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_packet_dump_packet_rx_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_packet_tx_diag_discard_packet_cmd);
    /* debug commands */
    return CLI_SUCCESS;
}

int32
ctc_goldengate_packet_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_packet_show_packet_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_packet_clear_packet_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_packet_show_packet_buffer_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_packet_clear_packet_buffer_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_packet_dump_packet_rx_cmd);

    /* debug commands */
    return CLI_SUCCESS;
}

