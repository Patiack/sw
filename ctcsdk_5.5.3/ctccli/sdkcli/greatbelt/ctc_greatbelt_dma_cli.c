/**
 @file ctc_dma_cli.c

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
#include "ctc_dma.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_greatbelt_dma_cli.h"
#include "sys_greatbelt_dma.h"

/* for testing */

extern int32
sys_greatbelt_dma_show_stats(uint8 lchip);

extern int32
sys_greatbelt_dma_show_state(uint8 lchip, uint32 detail);

extern int32
sys_greatbelt_dma_show_desc(uint8 lchip, ctc_dma_func_type_t type, uint8 chan_id);

extern int32
sys_greatbelt_dma_clear_stats(uint8 lchip);
extern int32
sys_greatbelt_dma_clear_pkt_stats(uint8 lchip, uint8 para);
int32
ctc_greatbelt_dump8(uint32 addr, uint32 len)
{
    uint8* data = (uint8*)((uintptr)addr);
    uint32 cnt = 0;
    char line[256];
    char tmp[32];

    if (0 == len)
    {
        return CTC_E_NONE;
    }

    for (cnt = 0; cnt < len; cnt++)
    {
        if ((cnt % 16) == 0)
        {
            if (cnt != 0)
            {
                ctc_cli_out("%s", line);
            }

            sal_memset(line, 0, sizeof(line));
            sal_sprintf(tmp, "\n%08X:  ", addr + cnt);
            sal_strcat(line, tmp);
        }

        sal_sprintf(tmp, "%02X", data[cnt]);
        sal_strcat(line, tmp);

        if ((cnt % 2) == 1)
        {
            sal_strcat(line, " ");
        }
    }

    ctc_cli_out("%s", line);
    ctc_cli_out("\n");

    return CTC_E_NONE;
}

CTC_CLI(ctc_cli_gb_dma_show_stats,
        ctc_cli_gb_dma_show_stats_cmd,
        "show stats dma (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Statistics",
        CTC_CLI_DMA_M_STR,
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 index = 0;
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_dma_show_stats(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}
CTC_CLI(ctc_cli_gb_dma_clear_dma_stats,
        ctc_cli_gb_dma_clear_dma_stats_cmd,
        "clear stats dma (rx | tx |) (lchip LCHIP|)",
        "Clear",
        "Statistics",
        CTC_CLI_DMA_M_STR,
        "Rx",
        "Tx",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 para = 0;

    index = CTC_CLI_GET_ARGC_INDEX("rx");
    if (0xFF != index)
    {
        para = 2;
    }
    index = CTC_CLI_GET_ARGC_INDEX("tx");
    if (0xFF != index)
    {
        para = 1;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_dma_clear_pkt_stats(lchip, para);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_dma_show_state,
        ctc_cli_gb_dma_show_state_cmd,
        "show dma state (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_DMA_M_STR,
        "State for global config",
        "Detail",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    int32 ret = CLI_SUCCESS;
    uint32 detail = FALSE;
    uint8 lchip = 0;
    uint32 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (0xFF != index)
    {
        detail = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_dma_show_state(lchip, detail);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_dma_mem_dump,
        ctc_cli_dma_mem_dump_cmd,
        "dma mem dump addr ADDR len LEN",
        "dma",
        "memory",
        "dump",
        "address",
        "address value",
        "dump mem len",
        "len value")
{
    uintptr addr = 0;
    uint32 len = 0;
    uint32 index = 0;
    int32 ret = 0;

    ctc_cli_out("Memory Dump:\n");

    /* parser fid */
    CTC_CLI_GET_UINT64("address", addr, argv[0]);

    /* parser key index */
    CTC_CLI_GET_UINT32("len", len, argv[1]);

    if (0)
    {
        for (index = 0; index < len; index++)
        {
            ctc_cli_out("%08X   ", *((uint32*)addr+index));

            if (((index+1) %8) == 0)
            {
                ctc_cli_out("\n");
            }
        }
    }
    else
    {
        ret = ctc_greatbelt_dump8(addr, len);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_show_dma_desc,
        ctc_cli_show_dma_desc_cmd,
        "show dma desc (stats | packet-rx chan CHAN | packet-tx chan CHAN | table-r | table-w) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "DMA module",
        "Descriptor",
        "Stats descriptor",
        "Packet RX descriptor",
        "DMA channel",
        "DMA channel ID",
        "Packet TX descriptor",
        "DMA channel",
        "DMA channel ID",
        "Table read descriptor",
        "Table write descriptor",
        CTC_CLI_CHIP_DESC,
        CTC_CLI_CHIP_ID_DESC)
{
    uint8 index = 0;
    uint8 chan_id = 0;
    int32 ret = 0;
    uint8 lchip = 0;
    ctc_dma_func_type_t func_type;

    if (0 == sal_strcmp(argv[0], "stats"))
    {
        func_type = CTC_DMA_FUNC_STATS;
    }
    else if(0 == sal_strcmp(argv[0], "packet-rx"))
    {
        func_type = CTC_DMA_FUNC_PACKET_RX;

        index = CTC_CLI_GET_ARGC_INDEX("chan");
        if (index != 0xFF)
        {
            CTC_CLI_GET_UINT8("chan id", chan_id, argv[index + 1]);
        }
    }
    else if(0 == sal_strcmp(argv[0], "packet-tx"))
    {
        func_type = CTC_DMA_FUNC_PACKET_TX;
        index = CTC_CLI_GET_ARGC_INDEX("chan");
        if (index != 0xFF)
        {
            CTC_CLI_GET_UINT8("chan id", chan_id, argv[index + 1]);
        }
    }
    else if(0 == sal_strcmp(argv[0], "table-r"))
    {
        func_type = CTC_DMA_FUNC_TABLE_R;
    }
    else if(0 == sal_strcmp(argv[0], "table-w"))
    {
        func_type = CTC_DMA_FUNC_TABLE_W;
    }
    else
    {
        return CLI_ERROR;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_dma_show_desc(lchip, func_type, chan_id);
    if (ret < 0)
    {
        return ret;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_dma_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_dma_show_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_dma_show_state_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_dma_mem_dump_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_show_dma_desc_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_dma_clear_dma_stats_cmd);

    return 0;
}

int32
ctc_greatbelt_dma_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_dma_show_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_dma_show_state_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_dma_mem_dump_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_show_dma_desc_cmd);

    return 0;
}

