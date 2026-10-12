/**
 @file ctc_greatbel_port_cli.c

 @date 2012-04-06

 @version v2.0

---file comments----
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_port_cli.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"

enum cli_port_mac_property_e
{
    CLI_PORT_MAC_PROPERTY_PADDING,
    CLI_PORT_MAC_PROPERTY_PREAMBLE,
    CLI_PORT_MAC_PROPERTY_CHKCRC,
    CLI_PORT_MAC_PROPERTY_STRIPCRC,
    CLI_PORT_MAC_PROPERTY_APPENDCRC,
    CLI_PORT_MAC_PROPERTY_ENTXERR,
    CLI_PORT_MAC_PROPERTY_CODEERR,
    CLI_PORT_MAC_PROPERTY_NUM
};
typedef enum cli_port_mac_property_e cli_port_mac_property_t;

extern int32 sys_goldengate_port_get_monitor_rlt(uint8 lchip);
extern int32 sys_goldengate_port_mac_set_property(uint8 lchip, uint16 gport, uint32 property, uint32 value);
extern int32 sys_goldengate_port_mac_get_property(uint8 lchip, uint16 gport, uint32 property, uint32* p_value);
extern int32 sys_goldengate_port_set_internal_property(uint8 lchip, uint16 gport, uint32 port_prop, uint32 value);
extern int32 sys_goldengate_port_show_port_maclink(uint8 lchip);
extern int32 sys_goldengate_port_set_3ap_training_en(uint8 lchip, uint16 gport, uint32 cfg_flag);
extern int32 sys_goldengate_port_get_3ap_training_en(uint8 lchip, uint16 gport, uint32* training_state);
extern int32 sys_goldengate_internal_port_set_iloop_for_pkt_to_cpu(uint8 lchip, uint32 gport, uint8 enable);
extern int32 sys_goldengate_port_set_monitor_link_enable(uint8 lchip, uint32 enable);
extern int32 sys_goldengate_port_get_monitor_link_enable(uint8 lchip, uint32* p_value);
extern int32 sys_goldengate_port_set_auto_neg_restart_enable(uint8 lchip, uint32 enable);
extern int32 sys_goldengate_port_get_auto_neg_restart_enable(uint8 lchip, uint32* p_value);
extern int32 sys_goldengate_port_get_code_error_count(uint8 lchip, uint32 gport, uint32* p_value);
extern int32 sys_goldengate_port_self_checking(uint8 lchip, uint32 gport);
CTC_CLI(ctc_cli_gg_port_set_vlan_range,
        ctc_cli_gg_port_set_vlan_range_cmd,
        "port GPHYPORT_ID vlan-range direction (ingress|egress)  vrange-grp VRANGE_GROUP_ID (enable|disable)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Vlan range on port",
        "Direction",
        "Ingress",
        "Egress",
        "Vlan range group id",
        "<0-63>",
        "Enable vlan range",
        "Disable vlan range")
{
    int32 ret = CLI_SUCCESS;
    uint16 gport = 0;
    bool enable = FALSE;
    ctc_vlan_range_info_t vrange_info;
    ctc_direction_t dir = CTC_INGRESS;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);


    if (CLI_CLI_STR_EQUAL("ingress", 1))
    {
        dir = CTC_INGRESS;
    }
    else if (CLI_CLI_STR_EQUAL("egress", 1))
    {
        dir = CTC_EGRESS;
    }

    CTC_CLI_GET_UINT8_RANGE("vlan range group id", vrange_info.vrange_grpid, argv[2], 0, CTC_MAX_UINT8_VALUE);

    if (CLI_CLI_STR_EQUAL("enable", 3))
    {
        enable = TRUE;
    }
    else if (CLI_CLI_STR_EQUAL("disable", 3))
    {
        enable = FALSE;
    }

    vrange_info.direction = dir;

    if(g_ctcs_api_en)
    {
       ret = ctcs_port_set_vlan_range(g_api_lchip, gport, &vrange_info, enable);
    }
    else
    {
        ret = ctc_port_set_vlan_range(gport, &vrange_info, enable);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_port_set_system_min_frame_size,
        ctc_cli_gg_port_set_system_min_frame_size_cmd,
        "system min-frame-size (size0|size1|size2|size3|size4|size5|size6|size7) BYTES",
        "System attribution",
        "Min frame size to apply port select",
        "Min frame size0",
        "Min Frame size1",
        "Min Frame size2",
        "Min Frame size3",
        "Min frame size4",
        "Min Frame size5",
        "Min Frame size6",
        "Min Frame size7",
        CTC_CLI_PORT_MIN_FRAME_SIZE)
{
    int32 ret = CLI_SUCCESS;
    uint16 min_size;
    ctc_frame_size_t index = CTC_FRAME_SIZE_0;

    CTC_CLI_GET_UINT16_RANGE("min frame size", min_size, argv[1], 0, CTC_MAX_UINT16_VALUE);

    if (CLI_CLI_STR_EQUAL("size0", 0))
    {
        index = CTC_FRAME_SIZE_0;
    }
    else if (CLI_CLI_STR_EQUAL("size1", 0))
    {
        index = CTC_FRAME_SIZE_1;
    }
    else if (CLI_CLI_STR_EQUAL("size2", 0))
    {
        index = CTC_FRAME_SIZE_2;
    }
    else if (CLI_CLI_STR_EQUAL("size3", 0))
    {
        index = CTC_FRAME_SIZE_3;
    }
    else if (CLI_CLI_STR_EQUAL("size4", 0))
    {
        index = CTC_FRAME_SIZE_4;
    }
    else if (CLI_CLI_STR_EQUAL("size5", 0))
    {
        index = CTC_FRAME_SIZE_5;
    }
    else if (CLI_CLI_STR_EQUAL("size6", 0))
    {
        index = CTC_FRAME_SIZE_6;
    }
    else if (CLI_CLI_STR_EQUAL("size7", 0))
    {
        index = CTC_FRAME_SIZE_7;
    }

    if(g_ctcs_api_en)
    {
       ret = ctcs_set_min_frame_size(g_api_lchip, index, min_size);
    }
    else
    {
        ret = ctc_set_min_frame_size(index, min_size);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_port_show_system_min_frame_size,
        ctc_cli_gg_port_show_system_min_frame_size_cmd,
        "show system min-frame-size",
        CTC_CLI_SHOW_STR,
        "System attribution",
        "Min frame size to apply port select")
{
    int32 ret = CLI_SUCCESS;
    uint8 size_idx = 0;
    uint16 val_16 = 0;

    ctc_cli_out("Show system min frame size.\n");
    ctc_cli_out("--------------\n");
    ctc_cli_out("%-8s%s\n", "Index", "Value");
    ctc_cli_out("--------------\n");

    for (size_idx = 0; size_idx < CTC_FRAME_SIZE_MAX; size_idx++)
    {
        if (g_ctcs_api_en)
        {
            ret = ctcs_get_min_frame_size(g_api_lchip, size_idx, &val_16);
        }
        else
        {
            ret = ctc_get_min_frame_size(size_idx, &val_16);
        }
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }

        ctc_cli_out("%-8u%u\n", size_idx, val_16);
    }
    ctc_cli_out("--------------\n");

    return CTC_E_NONE;
}

CTC_CLI(ctc_cli_gg_port_set_mac_property,
        ctc_cli_gg_port_set_mac_property_cmd,
        "port GPHYPORT_ID (check-crc|strip-crc|append-crc|tx-error) (enable|disable) (lchip LCHIP|)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Check crc",
        "Strip crc",
        "Append crc",
        "Tx error",
        "enable",
        "disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;
    uint16 gport = 0;
    uint32 value = 0;
    uint32 property = 0;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("check-crc");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_CHKCRC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("strip-crc");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_STRIPCRC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("append-crc");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_APPENDCRC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("tx-error");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_ENTXERR;
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        value = (CLI_PORT_MAC_PROPERTY_ENTXERR == property) ? 0 : 1;

    }

    index = CTC_CLI_GET_ARGC_INDEX("disable");
    if (0xFF != index)
    {
        value = (CLI_PORT_MAC_PROPERTY_ENTXERR == property) ? 1 : 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_mac_set_property(lchip, gport, property, value);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_gg_port_set_port_sec,
        ctc_cli_gg_port_set_port_sec_cmd,
        "port GPHYPORT_ID port-security (enable|disable) (lchip LCHIP|)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Port security",
        "enable",
        "disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint16 gport = 0;
    uint32 value = 0;
    uint32 property = 0;
    uint8 index = 0;


    /* SYS_PORT_PROP_SECURITY_EN */
    property = 11;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    if(0 == sal_memcmp(argv[1], "enable", sal_strlen("enable")))
    {
        value = 1;
    }
    else
    {
        value = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_set_internal_property(lchip, gport, property, value);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_port_get_mac_property,
        ctc_cli_gg_port_get_mac_property_cmd,
        "show port GPHYPORT_ID (check-crc|strip-crc|append-crc|tx-error|code-error) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Check crc",
        "Strip crc",
        "Append crc",
        "Tx error",
        "code-error",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;
    uint16 gport = 0;
    uint32 value = 0;
    uint32 property = 0;
    char* p_desc[] = {"padding", "preamble", "check crc", "strip crc", "append crc", "tx error", NULL};

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("check-crc");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_CHKCRC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("strip-crc");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_STRIPCRC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("append-crc");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_APPENDCRC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("tx-error");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_ENTXERR;
    }

    index = CTC_CLI_GET_ARGC_INDEX("code-error");
    if (0xFF != index)
    {
        property = CLI_PORT_MAC_PROPERTY_CODEERR;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_mac_get_property(lchip, gport, property, &value);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }
    else
    {
        if (CLI_PORT_MAC_PROPERTY_CODEERR == property)
        {
            ctc_cli_out("GPort:0x%04X, CodeErr:0x%x\n", gport, value);
        }
        else
        {
            value = (CLI_PORT_MAC_PROPERTY_ENTXERR == property) ? !value : value;

            if (!value)
            {
                ctc_cli_out("GPort:0x%04X, %s disable\n", gport, p_desc[property]);
            }
            else
            {
                ctc_cli_out("GPort:0x%04X, %s enable\n", gport, p_desc[property]);
            }
        }
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_port_show_port_maclink,
        ctc_cli_gg_port_show_port_maclink_cmd,
        "show port mac-link (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        "Port mac link status",
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
    ret = sys_goldengate_port_show_port_maclink(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gg_port_show_monitor_rlt,
        ctc_cli_gg_port_show_monitor_rlt_cmd,
        "show port link monitor  (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Port module",
        "Link status",
        "Monitor",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0;
    uint8 index = 0;
    int32 ret = CLI_SUCCESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_get_monitor_rlt(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gg_port_get_3ap_status,
        ctc_cli_gg_port_get_3ap_status_cmd,
        "show port GPHYPORT_ID 3ap-status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "802.3ap training status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;
    uint32 gport = 0;
    uint32 value = 0;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);
    ctc_cli_out("%-45s:  0x%04x\n","Show GPort", gport);
    ctc_cli_out("--------------------------------------------------------\n");

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;

    ret = sys_goldengate_port_get_3ap_training_en(lchip, gport, &value);
    if (ret >= 0)
    {
        ctc_cli_out("%-45s:  %u\n", "3ap Status", value);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_gg_port_set_3ap_training_en,
        ctc_cli_gg_port_set_3ap_training_en_cmd,
        "port GPHYPORT_ID 3ap-train (enable | init | done |disable)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "802.3ap training cfg flag",
        "Enable",
        "Init",
        "Done",
        "Disable")
{
    int32 ret = 0;
    uint16 gport;
    uint16 lport = 0;
    uint32 cfg_flag;
    uint8 lchip = 0;
    uint8 index = 0;

    lport = lport;
    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    if (CLI_CLI_STR_EQUAL("enable", 1))
    {
        cfg_flag = 1;
    }
    else if (CLI_CLI_STR_EQUAL("init", 1))
    {
        cfg_flag = 2;
    }
    else if (CLI_CLI_STR_EQUAL("done", 1))
    {
        cfg_flag = 3;
    }
    else
    {
        cfg_flag = 0;
    }

    ret = sys_goldengate_port_set_3ap_training_en(lchip, gport, cfg_flag);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_port_set_pkt_to_cpu_en,
        ctc_cli_gg_port_set_pkt_to_cpu_en_cmd,
        "port GPHYPORT_ID pkt-to-cpu (enable | disable)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Send packet to CPU",
        "Enable",
        "Disable")
{
    int32 ret = 0;
    uint16 gport;
    uint8 enable = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (index != 0xFF)
    {
        enable = 1;
    }

    ret = sys_goldengate_internal_port_set_iloop_for_pkt_to_cpu(0, gport, enable);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_port_set_monitor_link,
        ctc_cli_gg_port_set_monitor_link_cmd,
        "port link monitor ( enable | disable ) (lchip LCHIP|)",
        CTC_CLI_PORT_M_STR,
        "Link status",
        "Monitor port link",
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 value = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        value = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("disable");
    if (0xFF != index)
    {
        value = 0;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_set_monitor_link_enable(lchip, value);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_port_get_monitor_link,
        ctc_cli_gg_port_get_monitor_link_cmd,
        "show port link monitor status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        "Link status",
        "Monitor port link",
        "Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 value = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_get_monitor_link_enable(lchip, &value);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("%-45s:  %s\n","Link Monitor", value?"enable":"disable");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_port_set_restart_auto_neg,
        ctc_cli_gg_port_set_restart_auto_neg_cmd,
        "port cl73-auto-neg restart ( enable | disable ) (lchip LCHIP|)",
        CTC_CLI_PORT_M_STR,
        "Cl73 auto neg",
        "Restart",
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 value = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        value = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("disable");
    if (0xFF != index)
    {
        value = 0;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_set_auto_neg_restart_enable(lchip, value);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_port_get_restart_auto_neg,
        ctc_cli_gg_port_get_restart_auto_neg_cmd,
        "show port cl73-auto-neg restart status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        "Cl73-auto-neg",
        "Restart",
        "Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 value = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_get_auto_neg_restart_enable(lchip, &value);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("%-45s:  %s\n","Restart auto-neg", value?"enable":"disable");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_port_get_code_error_count,
        ctc_cli_gg_port_get_code_error_count_cmd,
        "show port GPHYPORT_ID code-error-count (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Code error count",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;
    uint32 gport = 0;
    uint32 value = 0;

    CTC_CLI_GET_UINT32_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT32_VALUE);
    ctc_cli_out("%-45s:  0x%04x\n","Show GPort", gport);
    ctc_cli_out("--------------------------------------------------------\n");

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;

    ret = sys_goldengate_port_get_code_error_count(lchip, gport, &value);
    if (ret >= 0)
    {
        ctc_cli_out("%-45s:  %u\n", "Code Error", value);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_port_self_checking,
    ctc_cli_gg_port_self_checking_cmd,
    "port  GPORT self-checking (lchip LCHIP|)",
    CTC_CLI_PORT_M_STR,
    CTC_CLI_GPHYPORT_ID_DESC,
    "Self-checking",
    CTC_CLI_LCHIP_ID_STR,
    CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 index = 0;
    uint32 gport = 0;
    uint8 lchip = 0;
    int32  ret = CLI_SUCCESS;

    CTC_CLI_GET_UINT32_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT32_VALUE);
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_port_self_checking(lchip, gport);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_goldengate_port_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_port_set_system_min_frame_size_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_port_show_system_min_frame_size_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_port_set_vlan_range_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_mac_property_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_mac_property_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_port_sec_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_port_show_port_maclink_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_show_monitor_rlt_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_3ap_training_en_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_3ap_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_port_set_pkt_to_cpu_en_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_monitor_link_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_monitor_link_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_restart_auto_neg_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_restart_auto_neg_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_code_error_count_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_port_self_checking_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_port_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_port_set_system_min_frame_size_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_port_show_system_min_frame_size_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_port_set_vlan_range_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_mac_property_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_mac_property_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_port_sec_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_port_show_port_maclink_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_show_monitor_rlt_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_3ap_training_en_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_3ap_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_port_set_pkt_to_cpu_en_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_monitor_link_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_monitor_link_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_set_restart_auto_neg_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_restart_auto_neg_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_port_get_code_error_count_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_port_self_checking_cmd);

    return CLI_SUCCESS;
}

