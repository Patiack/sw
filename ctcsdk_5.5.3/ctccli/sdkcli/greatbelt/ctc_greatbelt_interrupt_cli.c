/**
 @file ctc_greatbelt_interrupt_cli.c

 @author  Copyright (C) 2012 Centec Networks Inc.  All rights reserved.

 @date 2012-10-23

 @version v2.0

 This file define interrupt CLI functions

*/

#include "sal.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_cli.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_interrupt.h"
#include "ctc_cli_common.h"
#include "ctc_greatbelt_interrupt.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_interrupt.h"

extern int32
sys_greatbelt_interrupt_dump_db(uint8 lchip);
extern int32
sys_greatbelt_interrupt_dump_reg(uint8 lchip, uint32 intr);
extern int32
sys_greatbelt_interrupt_parser_type(uint8 lchip, ctc_intr_type_t type);
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
extern int32
sys_greatbelt_interrupt_model_sim_trigger(uint8 lchip, ctc_intr_type_t* p_type);

CTC_CLI(ctc_cli_gb_interrupt_trigger_status,
        ctc_cli_gb_interrupt_trigger_status_cmd,
        "interrupt LCHIP type TYPE sub-type TYPE trigger",
        CTC_CLI_INTR_M_STR,
        CTC_CLI_CHIP_ID_DESC,
        CTC_CLI_INTR_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_INTR_SUB_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        "Trigger interrupt")
{
    uint8 lchip = 0;
    ctc_intr_type_t type;
    int32 ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT32("local-lchip", lchip, argv[0]);
    CTC_CLI_GET_UINT32("type", type.intr, argv[1]);
    CTC_CLI_GET_UINT32("sub-type", type.sub_intr, argv[2]);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_interrupt_model_sim_trigger(lchip, &type);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

#endif

CTC_CLI(ctc_cli_gb_interrupt_clear_status,
        ctc_cli_gb_interrupt_clear_status_cmd,
        "interrupt LCHIP type TYPE sub-type TYPE clear",
        CTC_CLI_INTR_M_STR,
        CTC_CLI_CHIP_ID_DESC,
        CTC_CLI_INTR_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_INTR_SUB_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        "Clear")
{
    uint8 lchip = 0;
    ctc_intr_type_t type;
    int32 ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT32("local-lchip", lchip, argv[0]);
    CTC_CLI_GET_UINT32("type", type.intr, argv[1]);
    CTC_CLI_GET_UINT32("sub-type", type.sub_intr, argv[2]);

    if(g_ctcs_api_en)
    {
        ret = ctcs_interrupt_clear_status(g_api_lchip, &type);
    }
    else
    {
        ret = ctc_interrupt_clear_status(lchip, &type);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_interrupt_get_status,
        ctc_cli_gb_interrupt_get_status_cmd,
        "show interrupt LCHIP type TYPE sub-type TYPE status",
        CTC_CLI_SHOW_STR,
        CTC_CLI_INTR_M_STR,
        CTC_CLI_CHIP_ID_DESC,
        CTC_CLI_INTR_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_INTR_SUB_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        "Status")
{
    uint8 lchip = 0;
    ctc_intr_type_t type;
    ctc_intr_status_t status;
    int32 ret = CLI_SUCCESS;

    sal_memset(&type, 0, sizeof(type));
    sal_memset(&status, 0, sizeof(status));

    CTC_CLI_GET_UINT32("local-lchip", lchip, argv[0]);
    CTC_CLI_GET_UINT32("type", type.intr, argv[1]);
    CTC_CLI_GET_UINT32("sub-type", type.sub_intr, argv[2]);

    if(g_ctcs_api_en)
    {
        ret = ctcs_interrupt_get_status(g_api_lchip, &type, &status);
    }
    else
    {
        ret = ctc_interrupt_get_status(lchip, &type, &status);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("bits %d\n", status.bit_count);

    if (0 == status.bit_count)
    {
        ctc_cli_out("invalid bits\n");
    }
    else if (CTC_UINT32_BITS >= status.bit_count)
    {
        ctc_cli_out("%08X\n", status.bmp[0]);
    }
    else if (CTC_UINT32_BITS * 2 >= status.bit_count)
    {
        ctc_cli_out("%08X %08X\n", status.bmp[0], status.bmp[1]);
    }
    else if (CTC_UINT32_BITS * 3 >= status.bit_count)
    {
        ctc_cli_out("%08X %08X %08X\n", status.bmp[0], status.bmp[1], status.bmp[2]);
    }
    else
    {
        ctc_cli_out("invalid bits\n");
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_interrupt_set_en,
        ctc_cli_gb_interrupt_set_en_cmd,
        "interrupt LCHIP type TYPE sub-type TYPE (enable|disable)",
        CTC_CLI_INTR_M_STR,
        CTC_CLI_CHIP_ID_DESC,
        CTC_CLI_INTR_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_INTR_SUB_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_ENABLE,
        CTC_CLI_DISABLE)
{
    uint8 lchip = 0;
    ctc_intr_type_t type;
    uint32 enable = 0;
    int32 ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT32("local-lchip", lchip, argv[0]);
    CTC_CLI_GET_UINT32("type", type.intr, argv[1]);
    CTC_CLI_GET_UINT32("sub-type", type.sub_intr, argv[2]);

    if (CLI_CLI_STR_EQUAL("en", 3))
    {
        enable = TRUE;
    }
    else
    {
        enable = FALSE;
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_interrupt_set_en(g_api_lchip, &type, enable);
    }
    else
    {
        ret = ctc_interrupt_set_en(lchip, &type, enable);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_interrupt_get_en,
        ctc_cli_gb_interrupt_get_en_cmd,
        "show interrupt LCHIP type TYPE sub-type TYPE enable",
        CTC_CLI_SHOW_STR,
        CTC_CLI_INTR_M_STR,
        CTC_CLI_CHIP_ID_DESC,
        CTC_CLI_INTR_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_INTR_SUB_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_ENABLE)
{
    uint8 lchip = 0;
    ctc_intr_type_t type;
    uint32 enable = 0;
    int32 ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT32("local-lchip", lchip, argv[0]);
    CTC_CLI_GET_UINT32("type", type.intr, argv[1]);
    CTC_CLI_GET_UINT32("sub-type", type.sub_intr, argv[2]);

    if(g_ctcs_api_en)
    {
        ret = ctcs_interrupt_get_en(g_api_lchip, &type, &enable);
    }
    else
    {
        ret = ctc_interrupt_get_en(lchip, &type, &enable);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("%d\n", enable);
    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_interrupt_show_interrupt_sdk,
        ctc_cli_gb_interrupt_show_interrupt_sdk_cmd,
        "show interrupt sdk (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_INTR_M_STR,
        "Infomation in SDK",
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
    ret = sys_greatbelt_interrupt_dump_db(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_interrupt_show_interrupt_chip,
        ctc_cli_gb_interrupt_show_interrupt_chip_cmd,
        "show interrupt LCHIP register (group | chip-fatal | chip-normal | func | dma-func | dma-normal | all)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_INTR_M_STR,
        CTC_CLI_CHIP_ID_DESC,
        "Interrupt register",
        "Group/PIN level",
        "Chip fatal",
        "Chip normal",
        "Function",
        "DMA function",
        "DMA normal",
        "All")
{
    uint8 lchip = 0;
    uint32 intr = 0;
    int32 ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT8("local-lchip", lchip, argv[0]);

    if (0 == sal_strcmp(argv[1], "group"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_MAX);
    }
    else if (0 == sal_strcmp(argv[1], "chip-fatal"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_CHIP_FATAL);
    }
    else if (0 == sal_strcmp(argv[1], "chip-normal"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_CHIP_NORMAL);
    }
    else if (0 == sal_strcmp(argv[1], "func"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_FUNC_PTP_TS_CAPTURE);
    }
    else if (0 == sal_strcmp(argv[1], "dma-func"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_DMA_FUNC);
    }
    else if (0 == sal_strcmp(argv[1], "dma-normal"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_DMA_NORMAL);
    }
    else if (0 == sal_strcmp(argv[1], "all"))
    {
        CTC_BIT_SET(intr, CTC_INTR_GB_CHIP_FATAL);
        CTC_BIT_SET(intr, CTC_INTR_GB_CHIP_NORMAL);
        CTC_BIT_SET(intr, CTC_INTR_GB_FUNC_PTP_TS_CAPTURE);
        CTC_BIT_SET(intr, CTC_INTR_GB_DMA_FUNC);
        CTC_BIT_SET(intr, CTC_INTR_GB_DMA_NORMAL);
        CTC_BIT_SET(intr, CTC_INTR_GB_MAX); /* for group */
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_interrupt_dump_reg(lchip, intr);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}
CTC_CLI(ctc_cli_gb_interrupt_show_type,
        ctc_cli_gb_interrupt_show_type_cmd,
        "show interrupt LCHIP type TYPE sub-type TYPE",
        CTC_CLI_SHOW_STR,
        CTC_CLI_INTR_M_STR,
        CTC_CLI_LCHIP_ID_VALUE,
        CTC_CLI_INTR_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE,
        CTC_CLI_INTR_SUB_TYPE_STR,
        CTC_CLI_INTR_TYPE_VALUE)
{
    uint8 lchip = 0;
    ctc_intr_type_t type;
    int32 ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT8("local-lchip", lchip, argv[0]);
    CTC_CLI_GET_UINT32("type", type.intr, argv[1]);
    CTC_CLI_GET_UINT32("sub-type", type.sub_intr, argv[2]);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_interrupt_parser_type(lchip, type);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_interrupt_cli_init(void)
{
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_trigger_status_cmd);
#endif
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_clear_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_get_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_set_en_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_get_en_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_show_interrupt_sdk_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_show_interrupt_chip_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_show_type_cmd);
    return CLI_SUCCESS;
}

int32
ctc_greatbelt_interrupt_cli_deinit(void)
{
#if (SDK_WORK_PLATFORM == 1 && SDK_WORK_ENV == 1)  /* simulation */
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_trigger_status_cmd);
#endif
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_clear_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_get_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_set_en_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_get_en_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_show_interrupt_sdk_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_show_interrupt_chip_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_interrupt_show_type_cmd);

    return CLI_SUCCESS;
}

