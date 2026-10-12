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
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_greatbelt_qos_cli.h"

extern int32 sys_greatbelt_qos_domain_map_default(uint8 lchip);
extern int32 sys_greatbelt_qos_domain_map_dump(uint8 lchip,uint8 domain, uint8 type);
extern int32 sys_greatbelt_qos_cpu_reason_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_greatbelt_qos_queue_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_greatbelt_qos_group_dump(uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_greatbelt_qos_channel_dump(uint8 lchip, uint32 start, uint32 end, uint8 detail);
extern int32 sys_greatbelt_qos_policer_dump(uint8 type, uint8 dir, uint8 lchip, uint16 start, uint16 end, uint8 detail);
extern int32 sys_greatbelt_qos_metfifo_dump(uint8 lchip);
extern char* sys_greatbelt_reason_2Str(uint8 lchip, uint16 reason_id);
extern int32 sys_greatbelt_queue_set_service_queue_mode(uint8 lchip, uint8 mode);
extern int32 sys_greatbelt_queue_set_c2c_queue_mode(uint8 lchip, uint8 mode);
extern int32 sys_greatbelt_qos_service_dump(uint8 lchip, uint16 service_id, uint32 gport, uint8 detail);
/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

#define CPU_REASON_MAX_INDEX \
    (CTC_PKT_CPU_REASON_CUSTOM_BASE)

CTC_CLI(ctc_cli_gb_qos_set_cpu_reason_gourp_mode,
        ctc_cli_gb_qos_set_cpu_reason_gourp_mode_cmd,
        "qos cpu-group (reason-group GROUP | fwd-group | remote-fwd-group | oam-fwd-group )  dest-to (local-cpu | dma)",
        CTC_CLI_QOS_STR,
        "Cpu group",
        "Excption Reason group",
        "<0-15>",
        "Fwd to cpu group",
        "Remote chip fwd to cpu group",
        "OAM Fwd to cpu group",
        "Destination to",
        "CPU",
        "DMA")
{
    int32 ret = CLI_SUCCESS;
    uint8 group_type = 0;
    uint8 index = 0;
    uint8 group_id = 0;
    uint16 mode = 0;
    ctc_qos_queue_cfg_t que_cfg;

    sal_memset(&que_cfg, 0, sizeof(ctc_qos_queue_cfg_t));

    /* Get Cpu Reason group */

    index = CTC_CLI_GET_ARGC_INDEX("reason-group");
    if (0xFF != index)
    {
        group_type = CTC_PKT_CPU_REASON_GROUP_EXCEPTION;
        CTC_CLI_GET_UINT8("cpu-reason-group", group_id, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("fwd-group");
    if (0xFF != index)
    {
        group_type = CTC_PKT_CPU_REASON_GROUP_FORWARD;
    }

    index = CTC_CLI_GET_ARGC_INDEX("remote-fwd-group");
    if (0xFF != index)
    {
        group_type = CTC_PKT_CPU_REASON_GROUP_REMOTE_FORWARD;
    }

    index = CTC_CLI_GET_ARGC_INDEX("oam-fwd-group");
    if (0xFF != index)
    {
        group_type = CTC_PKT_CPU_REASON_GROUP_OAM;
    }

    index = CTC_CLI_GET_ARGC_INDEX("local-cpu");
    if (0xFF != index)
    {
        mode = CTC_PKT_MODE_ETH;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dma");
    if (0xFF != index)
    {
        mode = CTC_PKT_MODE_DMA;
    }

    que_cfg.type = CTC_QOS_QUEUE_CFG_QUEUE_REASON_MODE;
    que_cfg.value.reason_mode.group_type = group_type;
    que_cfg.value.reason_mode.group_id = group_id;
    que_cfg.value.reason_mode.mode = mode;

    if (g_ctcs_api_en)
    {
        ret = ctcs_qos_set_queue(g_api_lchip, &que_cfg);
    }
    else
    {
        ret = ctc_qos_set_queue(&que_cfg);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_qos_show_cpu_reason,
        ctc_cli_gb_qos_show_cpu_reason_cmd,
        "show qos cpu-reason-id (lchip LCHIP|)",
        "Show",
        CTC_CLI_QOS_STR,
        "Cpu reason id for maping,destination,shaping",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint16 index = 0;
    uint8 lchip = 0;

    ctc_cli_out("show cpu reason-id:\n");
    ctc_cli_out("============================================\n");
    ctc_cli_out("%-10s %20s\n", "reason-id", "reason description");

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    for (index = 0; index < CPU_REASON_MAX_INDEX; index++)
    {
        ctc_cli_out("%-10d  %-20s\n", index, sys_greatbelt_reason_2Str(lchip, index));
    }

    ctc_cli_out("============================================\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_qos_show_element_info,
        ctc_cli_gb_qos_show_element_info_cmd,
        "show qos (((port-policer (in|out) |flow-policer (in|out|)| cpu-reason | queue | group | port) (range START END)) \
        | (service-id SERVICE port GPHYPORT_ID )) (detail|) (lchip LCHIP|)",
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
        "Channel range <0-63>",
        "Range",
        "Range start",
        "Range end",
        "Service id",
        "Value <1-0xFFFF>",
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 detail = 0;
    uint8 index  = 0;
    uint32 start = 0;
    uint32 end   = 0;
    uint8 lchip = 0;
    uint16 service_id = 0;
    uint32 gport = 0;
    uint8 dir   = CTC_INGRESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (0xFF != index)
    {
        detail = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("range");
    if (0xFF != index)
    {
        /* Get Start queue-id */
        CTC_CLI_GET_UINT32("start", start, argv[index + 1]);

        /* Get End queue-id */
        CTC_CLI_GET_UINT32("end", end, argv[index + 2]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("service-id");
    if (0xFF != index)
    {
        /* service id */
        CTC_CLI_GET_UINT16("service-id", service_id, argv[index + 1]);
        CTC_CLI_GET_UINT32("port", gport, argv[index + 3]);

        ret = sys_greatbelt_qos_service_dump(lchip, service_id, gport, detail);
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }     
    }
    else
    {
        index = CTC_CLI_GET_ARGC_INDEX("port-policer");
        if (0xFF != index)
        {
            index = CTC_CLI_GET_ARGC_INDEX("out");
            if (0xFF != index)
            {
                dir = CTC_EGRESS;
            }
            ret = sys_greatbelt_qos_policer_dump(CTC_QOS_POLICER_TYPE_PORT, dir, lchip, start, end, detail);
            if (ret < 0)
            {
                ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
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

            ret = sys_greatbelt_qos_policer_dump(CTC_QOS_POLICER_TYPE_FLOW, dir, lchip, start, end, detail);
            if (ret < 0)
            {
                ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
                return CLI_ERROR;
            }
        }

        index = CTC_CLI_GET_ARGC_INDEX("cpu-reason");
        if (0xFF != index)
        {
            ret = sys_greatbelt_qos_cpu_reason_dump(lchip, start, end, detail);
            if (ret < 0)
            {
                ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
                return CLI_ERROR;
            }
        }

        index = CTC_CLI_GET_ARGC_INDEX("queue");
        if (0xFF != index)
        {
            ret = sys_greatbelt_qos_queue_dump(lchip, start, end, detail);
            if (ret < 0)
            {
                ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
                return CLI_ERROR;
            }
        }

        index = CTC_CLI_GET_ARGC_INDEX("group");
        if (0xFF != index)
        {
            ret = sys_greatbelt_qos_group_dump(lchip, start, end, detail);
            if (ret < 0)
            {
                ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
                return CLI_ERROR;
            }
        }

        index = CTC_CLI_GET_ARGC_INDEX("port");
        if (0xFF != index)
        {
            ret = sys_greatbelt_qos_channel_dump(lchip, start, end, detail);
            if (ret < 0)
            {
                ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
                return CLI_ERROR;
            }
        }
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gb_qos_use_default_domain,
        ctc_cli_gb_qos_use_default_domain_cmd,
        "qos domain default (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        "QoS domain",
        "Default value",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 index  = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_qos_domain_map_default(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_qos_show_qos_domain,
        ctc_cli_gb_qos_show_qos_domain_cmd,
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
    uint8 index  = 0;
    uint8 domain = 0;
    uint8 type = 0;

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

    ret = sys_greatbelt_qos_domain_map_dump(lchip,domain, type);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_qos_show_qos_metfifo_excp,
        ctc_cli_gb_qos_show_qos_metfifo_excp_cmd,
        "show qos metifo-exception status (lchip LCHIP|)",
        "Show",
        CTC_CLI_QOS_STR,
        "Metfifo exception info",
        "Destmap and nexthop info",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index  = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_qos_metfifo_dump(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gb_qos_set_service_queue_mode,
        ctc_cli_gb_qos_set_service_queue_mode_cmd,
        "qos service-queue mode MODE (lchip LCHIP|)",
        CTC_CLI_QOS_STR,
        "Qos service queue",
        "Service-queue mode",
        "mode 0: 4 queue mode; mode 1: 8 queue mode",
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

    ret = sys_greatbelt_queue_set_service_queue_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_qos_set_c2c_queue_mode,
        ctc_cli_gb_qos_set_c2c_queue_mode_cmd,
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

    ret = sys_greatbelt_queue_set_c2c_queue_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_qos_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_qos_set_cpu_reason_gourp_mode_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_qos_use_default_domain_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_qos_domain_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_cpu_reason_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_element_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_qos_metfifo_excp_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_qos_set_service_queue_mode_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_qos_set_c2c_queue_mode_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_qos_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_qos_set_cpu_reason_gourp_mode_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_qos_use_default_domain_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_qos_domain_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_cpu_reason_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_element_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_qos_show_qos_metfifo_excp_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_qos_set_c2c_queue_mode_cmd);

    return CLI_SUCCESS;
}

