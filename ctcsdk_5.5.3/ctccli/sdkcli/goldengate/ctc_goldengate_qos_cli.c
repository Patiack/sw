/**
 @date 2009-12-22

 @version v2.0

---file comments----
*/

/****************************************************************************
 *
 * Header files
 *
 *****************************************************************************/
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_goldengate_qos_cli.h"

extern int32 sys_goldengate_qos_domain_map_default(uint8 lchip);
extern int32 sys_goldengate_qos_domain_map_dump(uint8 lchip, uint8 domain, uint8 type);
extern int32 sys_goldengate_qos_cpu_reason_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_goldengate_qos_queue_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_goldengate_qos_group_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_goldengate_qos_port_dump(uint8 lchip, uint32 start, uint32 end, uint8 detail);
extern int32 sys_goldengate_qos_policer_dump(uint8 type, uint8 dir, uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_goldengate_qos_dump_status(uint8 lchip);
extern int32 sys_goldengate_queue_set_egs_resrc_guarantee(uint8 lchip, uint8 enable);
extern int32 sys_goldengate_qos_set_shape_mode(uint8 lchip, uint8 mode);
extern int32 sys_goldengate_qos_policer_set_gran_mode(uint8 lchip, uint8 mode);
extern int32 sys_goldengate_queue_set_c2c_queue_mode(uint8 lchip, uint8 enq_mode);

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/


CTC_CLI(ctc_cli_gg_qos_show_element_info,
        ctc_cli_gg_qos_show_element_info_cmd,
        "show qos (port-policer (in|out) |flow-policer  (in|out|)| cpu-reason | queue | group | port) (range START END) (lchip LCHIP|)",
        "Show",
        CTC_CLI_QOS_STR,
        "Port policer ",
        "Ingress port",
        "Egress port",
        "Flow policer",
        "Ingress",
        "Egress",
        "Cpu reason range <0-max>",
        "Queue range <0-1023>",
        "Group range <0-255>",
        CTC_CLI_GPHYPORT_ID_DESC,
        "Range",
        "Range start",
        "Range end",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 detail = 0;
    uint8 index  = 0;
    uint32 start = 0;
    uint32 end   = 0;
    uint8 dir   = CTC_INGRESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("range");
    if (0xFF != index)
    {
        /* Get Start queue-id */
        CTC_CLI_GET_UINT32("start", start, argv[index + 1]);

        /* Get End queue-id */
        CTC_CLI_GET_UINT32("end", end, argv[index + 2]);

        if (start > end)
        {
            ctc_cli_out("%% %s \n", "Invalid start end value.");
            return CLI_ERROR;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("port-policer");
    if (0xFF != index)
    {
        index = CTC_CLI_GET_ARGC_INDEX("out");
        if (0xFF != index)
        {
            dir = CTC_EGRESS;
        }

        ret = sys_goldengate_qos_policer_dump(CTC_QOS_POLICER_TYPE_PORT, dir, lchip, start, end, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("flow-policer");
    if (0xFF != index)
    {
        index = CTC_CLI_GET_ARGC_INDEX("out");
        if (0xFF != index)
        {
            dir = CTC_EGRESS;
        }

        ret = sys_goldengate_qos_policer_dump(CTC_QOS_POLICER_TYPE_FLOW, dir, lchip, start, end, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("cpu-reason");
    if (0xFF != index)
    {
        ret = sys_goldengate_qos_cpu_reason_dump(lchip, start, end, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("queue");
    if (0xFF != index)
    {
        ret = sys_goldengate_qos_queue_dump(lchip, start, end, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("group");
    if (0xFF != index)
    {
        ret = sys_goldengate_qos_group_dump(lchip, start, end, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("port");
    if (0xFF != index)
    {
        ret = sys_goldengate_qos_port_dump(lchip, start, end, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gg_qos_use_default_domain,
        ctc_cli_gg_qos_use_default_domain_cmd,
        "qos domain default (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        "QoS domain",
        "Default value",
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
    ret = sys_goldengate_qos_domain_map_default(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}
CTC_CLI(ctc_cli_gg_qos_dump_status,
        ctc_cli_gg_qos_dump_status_cmd,
        "show qos status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_QOS_STR,
        "Resource used status",
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
    ret = sys_goldengate_qos_dump_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_qos_show_qos_domain,
        ctc_cli_gg_qos_show_qos_domain_cmd,
        "show qos domain DOAMIN type (cos-map-pri|ip-prec-map-pri|dscp-map-pri|exp-map-pri|pri-map-cos|pri-map-dscp|pri-map-exp) (lchip LCHIP|)",
        "Show",
        CTC_CLI_QOS_STR,
        "QoS domain",
        "domain <0-7>",
        "Maping type",
        "Cos map to priority color",
        "Ip-prec map to priority color",
        "Dscp map to priority color",
        "Exp map to priority color",
        "Priority color map to cos",
        "Priority color map to dscp",
        "Priority color map to exp",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 domain = 0;
    uint8 type = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT8_RANGE("domain", domain, argv[0], 0, CTC_MAX_UINT8_VALUE);

    if (CLI_CLI_STR_EQUAL("cos-map-pri", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_IGS_COS_TO_PRI_COLOR;
    }

    if (CLI_CLI_STR_EQUAL("ip-prec-map-pri", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_IGS_IP_PREC_TO_PRI_COLOR;
    }

    if (CLI_CLI_STR_EQUAL("dscp-map-pri", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_IGS_DSCP_TO_PRI_COLOR;
    }

    if (CLI_CLI_STR_EQUAL("exp-map-pri", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_IGS_EXP_TO_PRI_COLOR;
    }

    if (CLI_CLI_STR_EQUAL("pri-map-cos", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_COS;
    }

    if (CLI_CLI_STR_EQUAL("pri-map-dscp", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_DSCP;
    }

    if (CLI_CLI_STR_EQUAL("pri-map-exp", 1))
    {
        type = CTC_QOS_DOMAIN_MAP_EGS_PRI_COLOR_TO_EXP;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }


    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_qos_domain_map_dump(lchip, domain, type);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_qos_set_resrc_mgr_guarantee_enable,
        ctc_cli_gg_qos_set_resrc_mgr_guarantee_enable_cmd,
        "qos resrc-mgr guarantee (enable | disable) (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        "Resource manage",
        "Guarantee resource",
        "Globally enable",
        "Globally disable",
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

    if (CLI_CLI_STR_EQUAL("e", 0))
    {
        ret = sys_goldengate_queue_set_egs_resrc_guarantee(lchip, 1);
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }
    else
    {
        ret = sys_goldengate_queue_set_egs_resrc_guarantee(lchip, 0);
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gg_qos_set_shape_mode,
        ctc_cli_gg_qos_set_shape_mode_cmd,
        "qos shape mode MODE (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        CTC_CLI_QOS_SHAPE_STR,
        "Shape mode",
        "mode <0-1>",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 mode = 0;

    CTC_CLI_GET_UINT8_RANGE("mode", mode, argv[0], 0, CTC_MAX_UINT8_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_goldengate_qos_set_shape_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_qos_set_policer_gran_mode,
        ctc_cli_gg_qos_set_policer_gran_mode_cmd,
        "qos policer-gran mode MODE (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        "Qos policer granularity",
        "Policer-gran mode",
        "mode 0: Default mode; mode 1: TCP mode",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 mode = 0;

    CTC_CLI_GET_UINT8_RANGE("mode", mode, argv[0], 0, CTC_MAX_UINT8_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_goldengate_qos_policer_set_gran_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_qos_set_c2c_queue_mode,
        ctc_cli_gg_qos_set_c2c_queue_mode_cmd,
        "qos c2c-queue mode MODE (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        "Qos c2c queue",
        "C2C-queue mode",
        "Mode 0: 8 queue mode; mode 1: 16 queue mode",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 mode = 0;

    CTC_CLI_GET_UINT8_RANGE("mode", mode, argv[0], 0, CTC_MAX_UINT8_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_goldengate_queue_set_c2c_queue_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_qos_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_qos_use_default_domain_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_qos_show_qos_domain_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_qos_show_element_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_qos_dump_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_qos_set_resrc_mgr_guarantee_enable_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_qos_set_shape_mode_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_qos_set_policer_gran_mode_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_qos_set_c2c_queue_mode_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_qos_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_qos_use_default_domain_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_qos_show_qos_domain_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_qos_show_element_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_qos_dump_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_qos_set_resrc_mgr_guarantee_enable_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_qos_set_shape_mode_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_qos_set_policer_gran_mode_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_qos_set_c2c_queue_mode_cmd);

    return CLI_SUCCESS;
}

