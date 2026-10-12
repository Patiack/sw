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
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"
#include "ctc_cli_common.h"


extern int32 sys_greatbelt_port_get_monitor_rlt(uint8 lchip);
extern uint8 sys_greatebelt_common_get_mac_id(uint8 lchip, uint16 gport);
extern int32 sys_greatbelt_port_set_sgmii_unidir_en(uint8 lchip, uint16 gport, ctc_direction_t dir, bool enable);
extern int32 sys_greatbelt_port_get_sgmii_unidir_en(uint8 lchip, uint16 gport, ctc_direction_t dir, uint32* p_value);
extern int32 sys_greatbelt_port_set_monitor_link_enable(uint8 lchip, uint32 enable);
extern int32 sys_greatbelt_port_get_monitor_link_enable(uint8 lchip, uint32* p_value);
extern int32 sys_greatbelt_port_get_code_error_count(uint8 lchip, uint32 gport, uint32* p_value);
CTC_CLI(ctc_cli_gb_port_set_scl_key_type,
        ctc_cli_gb_port_set_scl_key_type_cmd,
        "port GPHYPORT_ID scl-property scl-id SCL_ID direction (ingress|egress) " \
        "type (cvid|svid|cvid-ccos|svid-scos|port|dvid|mac-sa|mac-da|ipv4|ipv6|ipsg-port-mac|ipsg-port-ip|ipsg-ipv6|tunnel ( rpf | )| tunnel-v6 ( rpf | )|disable)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "SCL key type on port",
        "SCL id",
        CTC_CLI_SCL_ID_VALUE,
        "Direction",
        "Ingress",
        "Egress",
        "Type",
        "Cvid mapping",
        "Svid mapping",
        "Cvid+ccos mapping",
        "Svid+scos mapping",
        "Port mapping",
        "Double vlan mapping",
        "Mac SA based on vlan class",
        "Mac DA based on vlan class",
        "Ipv4 based on vlan class",
        "Ipv6 based on vlan class",
        "IP source guard based on port and mac",
        "IP source guard based on port and ip",
        "IP source guard based on ipv6",
        "IP tunnel for IPv4/IPv6 in IPv4, 6to4, ISATAP, GRE with/without  GRE key tunnel",
        "Rpf check for outer header, if set, scl-id parameter must be 1, scl-id 0 for tunnel scl-id 1 for rpf",
        "IPv6 based IP tunnel",
        "Rpf check for outer header, if set, scl-id parameter must be 1, scl-id 0 for tunnel-v6 scl-id 1 for rpf",
        "Disable scl key")
{
    int32 ret = 0;
    uint16 gport = 0;
    uint8 scl_id = 0;
    ctc_port_scl_key_type_t type = 0;
    ctc_direction_t dir = CTC_INGRESS;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);


    CTC_CLI_GET_UINT8_RANGE("scl-key-type scl id", scl_id, argv[1], 0, CTC_MAX_UINT8_VALUE);
    if (CTC_CLI_STR_EQUAL_ENHANCE("ingress", 2))
    {
        dir = CTC_INGRESS;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("egress", 2))
    {
        dir = CTC_EGRESS;
    }

    if (CTC_CLI_STR_EQUAL_ENHANCE("cvid-ccos", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID_CCOS;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("svid-scos", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID_SCOS;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("cvid", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_MAPPING_CVID;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("svid", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_MAPPING_SVID;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("port", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_MAPPING_PORT;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("dvid", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_MAPPING_DVID;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("ipv4", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_CLASS_IPV4;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("ipv6", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_CLASS_IPV6;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("mac-sa", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_CLASS_MAC_SA;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("mac-da", 3))
    {
        type = CTC_SCL_KEY_TYPE_VLAN_CLASS_MAC_DA;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("ipsg-port-mac", 3))
    {
        type = CTC_SCL_KEY_TYPE_IPSG_PORT_MAC;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("ipsg-port-ip", 3))
    {
        type = CTC_SCL_KEY_TYPE_IPSG_PORT_IP;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("ipsg-ipv6", 3))
    {
        type = CTC_SCL_KEY_TYPE_IPSG_IPV6;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("tunnel", 3))
    {
        if (0xFF != CTC_CLI_GET_ARGC_INDEX("rpf"))
        {
            type = CTC_SCL_KEY_TYPE_IPV4_TUNNEL_WITH_RPF;
        }
        else
        {
            type = CTC_SCL_KEY_TYPE_IPV4_TUNNEL;
        }
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("tunnel-v6", 3))
    {
        if (0xFF != CTC_CLI_GET_ARGC_INDEX("rpf"))
        {
            type = CTC_SCL_KEY_TYPE_IPV6_TUNNEL_WITH_RPF;
        }
        else
        {
            type = CTC_SCL_KEY_TYPE_IPV6_TUNNEL;
        }
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("disable", 3))
    {
        type = CTC_SCL_KEY_TYPE_DISABLE;
    }
    else
    {
    }
    if(g_ctcs_api_en)
    {
        ret = ctcs_port_set_scl_key_type(g_api_lchip, gport, dir, scl_id, type);
    }
    else
    {
        ret = ctc_port_set_scl_key_type(gport, dir, scl_id, type);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_port_set_scl_property,
        ctc_cli_gb_port_set_scl_property_cmd,
        "port GPHYPORT_ID scl-property scl-id SCL_ID direction (ingress|egress) " \
        "hash-type (port-2vlan|port-svlan|port-cvlan|port-svlan-cos|port-cvlan-cos|mac-sa|mac-da|port-mac-sa|port-mac-da|" \
        "ip-sa|port-ip-sa|port|l2|tunnel|tunnel-rpf|disable) (tcam-type (mac|ip|vlan|tunnel|tunnel-rpf|disable)|)" \
        "({class-id CLASS_ID | use-logic-port}|)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "SCL property on port",
        "SCL id",
        CTC_CLI_SCL_ID_VALUE,
        "Direction",
        "Ingress",
        "Egress",
        "Hash Type",
        "Hash 2vlan",
        "Hash svlan",
        "Hash cvlan",
        "Hash svlan cos",
        "Hash cvlan cos",
        "Hash mac sa",
        "Hash mac da",
        "Hash port mac sa",
        "Hash port mac da",
        "Hash ip",
        "Hash port ip",
        "Hash port",
        "Hash l2",
        "Hash ip-tunnel",
        "Hash ip-tunnel rpf check",
        "Hash disable",
        "Tcam Type",
        "Tcam mac",
        "Tcam ip",
        "Tcam vlan",
        "Tcam ip-tunnel",
        "Tcam ip-tunnel rpf check",
        "Tcam disable",
        "Class id",
        "1-61",
        "Use logic port as global port")
{
    int32 ret = 0;
    uint16 gport = 0;
    uint8 index = 0xFF;
    ctc_port_scl_property_t port_scl_property;
    sal_memset(&port_scl_property, 0, sizeof(ctc_port_scl_property_t));

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);


    CTC_CLI_GET_UINT8_RANGE("scl id", port_scl_property.scl_id, argv[1], 0, CTC_MAX_UINT8_VALUE);

    index = 3; /* hash type*/
    if (CTC_CLI_STR_EQUAL_ENHANCE("ingress", 2))
    {
        port_scl_property.direction = CTC_INGRESS;
        if (CTC_CLI_STR_EQUAL_ENHANCE("port-2vlan", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_2VLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-svlan-cos", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN_COS;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-cvlan-cos", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN_COS;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-svlan", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_SVLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-cvlan", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_CVLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("mac-sa", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_MAC_SA;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("mac-da", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_MAC_DA;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-mac-sa", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_SA;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-mac-da", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_MAC_DA;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("ip-sa", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_IP_SA;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-ip-sa", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT_IP_SA;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("l2", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_L2;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("tunnel", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("tunnel-rpf", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_TUNNEL_RPF;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("disable", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_DISABLE;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port", index))
        {
            port_scl_property.hash_type = CTC_PORT_IGS_SCL_HASH_TYPE_PORT;
        }

    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("egress", 2))
    {
        port_scl_property.direction = CTC_EGRESS;
        if (CTC_CLI_STR_EQUAL_ENHANCE("port-2vlan", index))
        {
            port_scl_property.hash_type = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_2VLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-svlan-cos", index))
        {
            port_scl_property.hash_type = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN_COS;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-cvlan-cos", index))
        {
            port_scl_property.hash_type = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN_COS;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-svlan", index))
        {
            port_scl_property.hash_type = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_SVLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port-cvlan", index))
        {
            port_scl_property.hash_type = CTC_PORT_EGS_SCL_HASH_TYPE_PORT_CVLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("port", index))
        {
            port_scl_property.hash_type = CTC_PORT_EGS_SCL_HASH_TYPE_PORT;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("tcam-type");
    if (INDEX_VALID(index))
    {
        index ++;
        if (CTC_CLI_STR_EQUAL_ENHANCE("mac", index))
        {
            port_scl_property.tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_MAC;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("ip", index))
        {
            port_scl_property.tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_IP;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("vlan", index))
        {
            port_scl_property.tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_VLAN;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("tunnel", index))
        {
            port_scl_property.tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("tunnel-rpf", index))
        {
            port_scl_property.tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_TUNNEL_RPF;
        }
        else if (CTC_CLI_STR_EQUAL_ENHANCE("disable", index))
        {
            port_scl_property.tcam_type = CTC_PORT_IGS_SCL_TCAM_TYPE_DISABLE;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("class-id");
    if (INDEX_VALID(index))
    {
        port_scl_property.class_id_en = TRUE;
        CTC_CLI_GET_UINT16("class id", port_scl_property.class_id, argv[index+1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("use-logic-port");
    if (INDEX_VALID(index))
    {
        port_scl_property.use_logic_port_en = 1;
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_port_set_scl_property(g_api_lchip, gport, &port_scl_property);
    }
    else
    {
        ret = ctc_port_set_scl_property(gport, &port_scl_property);
    }


    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}



CTC_CLI(ctc_cli_gb_port_set_vlan_range,
        ctc_cli_gb_port_set_vlan_range_cmd,
        "port GPHYPORT_ID vlan-range direction (ingress|egress)  vrange-grp VRANGE_GROUP_ID (enable|disable)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Vlan range on port",
        "Direction",
        "Ingress",
        "Egress",
        "Vlan range group id",
        "<0~63>",
        "Enable vlan range",
        "Disable vlan range")
{
    int32 ret = 0;
    uint16 gport = 0;
    bool enable = FALSE;
    ctc_vlan_range_info_t vrange_info;
    ctc_direction_t dir = CTC_INGRESS;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);


    if (CTC_CLI_STR_EQUAL_ENHANCE("ingress", 1))
    {
        dir = CTC_INGRESS;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("egress", 1))
    {
        dir = CTC_EGRESS;
    }

    CTC_CLI_GET_UINT8_RANGE("vlan range group id", vrange_info.vrange_grpid, argv[2], 0, CTC_MAX_UINT8_VALUE);

    if (CTC_CLI_STR_EQUAL_ENHANCE("enable", 3))
    {
        enable = TRUE;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("disable", 3))
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

CTC_CLI(ctc_cli_port_show_monitor_rlt,
        ctc_cli_port_show_monitor_rlt_cmd,
        "show port link monitor  (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Port module",
        "Link status",
        "Monitor",
        "Local chip Id",
        "<0~1>",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0;
    uint8 index = 0;
    int32 ret = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if(0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("lchip id", lchip, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_port_get_monitor_rlt(lchip);
    if(ret != CTC_E_NONE)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gb_port_show_port_mac_link,
        ctc_cli_gb_port_show_port_mac_link_cmd,
        "show port mac-link (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        "Port mac link status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 lchip = 0, lchip_num = 0;
    uint8 gchip_id = 0;
    uint8 lport = 0;
    uint16 gport = 0;
    bool value_link_up = FALSE;
    bool value_mac_en = FALSE;
    uint32 value_32 = 0;
    char value_c[32] = {0};
    int32 ret = 0;
    uint8 mac_id = 0;
    uint8 port_type = 0;
    uint8 index = 0;
    uint32 max_phy_port = 0;
    uint32 capability[CTC_GLOBAL_CAPABILITY_MAX] = {0};

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    if(g_ctcs_api_en)
    {
        ret = ctcs_get_local_chip_num(g_api_lchip, &lchip_num);
    }
    else
    {
        ret = ctc_get_local_chip_num(&lchip_num);
    }
    if (0 != ret)
    {
        ctc_cli_out("%% Get Local Chip Number Error!\n");
        return CLI_ERROR;
    }
        if(g_ctcs_api_en)
        {
            ctcs_get_gchip_id(lchip, &gchip_id);
        }
        else
        {
            ctc_get_gchip_id(lchip, &gchip_id);
        }
        ctc_cli_out("Show Local Chip %d Port Link:\n", lchip);

        ctc_cli_out("--------------------------------------------------------\n");
        ctc_cli_out("%-8s%-6s%-7s%-9s%-7s%-8s%-10s%s\n","Port","MAC","Link","MAC-EN","Speed","Duplex","Auto-Neg","Interface");

        if(g_ctcs_api_en)
        {
            ret = ctcs_global_ctl_get(g_api_lchip, CTC_GLOBAL_CHIP_CAPABILITY, capability);
        }
        else
        {
            ret = ctc_global_ctl_get(CTC_GLOBAL_CHIP_CAPABILITY, capability);
        }
        max_phy_port = capability[CTC_GLOBAL_CAPABILITY_MAX_PHY_PORT_NUM];

        for (lport = 0; lport < max_phy_port; lport++)
        {
            gport = (gchip_id << 8) | lport;
            mac_id = sys_greatebelt_common_get_mac_id(lchip, gport);
            if(g_ctcs_api_en)
            {
                ret = ctcs_port_get_mac_link_up(lchip, gport, &value_link_up);
            }
            else
            {
                ret = ctc_port_get_mac_link_up(gport, &value_link_up);
            }
            if (ret < 0 )
            {
                continue;
            }
            if(g_ctcs_api_en)
            {
                ret = ctcs_port_get_mac_en(lchip, gport, &value_mac_en);
            }
            else
            {
                ret = ctc_port_get_mac_en(gport, &value_mac_en);
            }
            if (ret < 0)
            {
                continue;
            }

            if (0xFF == mac_id)
            {
                continue;
            }

            ctc_cli_out("0x%04x  ", gport);
            ctc_cli_out("%-6d",mac_id);
            if (value_link_up)
            {
                ctc_cli_out("%-7s", "up");
            }
            else
            {
                ctc_cli_out("%-7s", "down");
            }

            ctc_cli_out("%-9s", (value_mac_en ? "TRUE":"FALSE"));

            if(g_ctcs_api_en)
            {
                ret = ctcs_port_get_speed(lchip, gport, (ctc_port_speed_t*)&value_32);
            }
            else
            {
                ret = ctc_port_get_speed(gport, (ctc_port_speed_t*)&value_32);
            }
            if (ret < 0)
            {
                ctc_cli_out("%-7s","-");
            }
            else
            {
                sal_memset(value_c,0,sizeof(value_c));
                if ( CTC_PORT_SPEED_1G == value_32 )
                {
                    sal_memcpy(value_c,"1G",sal_strlen("1G"));
                }
                else if ( CTC_PORT_SPEED_100M == value_32 )
                {
                    sal_memcpy(value_c,"100M",sal_strlen("100M"));
                }
                else if ( CTC_PORT_SPEED_10M == value_32 )
                {
                    sal_memcpy(value_c,"10M",sal_strlen("10M"));
                }
                else if ( CTC_PORT_SPEED_2G5 == value_32 )
                {
                    sal_memcpy(value_c,"2.5G",sal_strlen("2.5G"));
                }
                else if ( CTC_PORT_SPEED_10G == value_32 )
                {
                    sal_memcpy(value_c,"10G",sal_strlen("10G"));
                }
                ctc_cli_out("%-7s", value_c);
            }

            ctc_cli_out("%-8s","FD");

            if(g_ctcs_api_en)
            {
                ret = ctcs_port_get_property(lchip, gport, CTC_PORT_PROP_AUTO_NEG_EN, &value_32);
            }
            else
            {
                ret = ctc_port_get_property(gport, CTC_PORT_PROP_AUTO_NEG_EN, &value_32);
            }
            if (ret < 0)
            {
                ctc_cli_out("%-10s","-");
            }
            else
            {
                ctc_cli_out("%-10s", (value_32 ? "TRUE":"FALSE"));
            }

            /*need modify by call sys*/
            if (0 == port_type)
            {
                ctc_cli_out("GMAC");
            }
            else if (1 == port_type)
            {
                ctc_cli_out("SGMAC");
            }

            ctc_cli_out("\n");
        }

        ctc_cli_out("--------------------------------------------------------\n");
        ctc_cli_out("\n");


    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_port_set_unidir,
        ctc_cli_gb_port_set_unidir_cmd,
        "port GPHYPORT_ID direction (ingress|egress) unidir (enable|disable) (lchip LCHIP|)",
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Direction",
        "Ingress",
        "Egress",
        "Unidir",
        "Enable unidir",
        "Disable unidir",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint16 gport = 0;
    bool enable = FALSE;
    uint8 lchip = 0;
    uint8  index = 0;
    ctc_direction_t dir = CTC_INGRESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);


    if (CTC_CLI_STR_EQUAL_ENHANCE("ingress", 1))
    {
        dir = CTC_INGRESS;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("egress", 1))
    {
        dir = CTC_EGRESS;
    }

    if (CTC_CLI_STR_EQUAL_ENHANCE("enable", 2))
    {
        enable = TRUE;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("disable", 2))
    {
        enable = FALSE;
    }

    ret = sys_greatbelt_port_set_sgmii_unidir_en(lchip, gport, dir, enable);
    if(ret != CTC_E_NONE)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_port_show_unidir,
        ctc_cli_gb_port_show_unidir_cmd,
        "show port GPHYPORT_ID direction (ingress|egress) unidir (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_PORT_M_STR,
        CTC_CLI_GPHYPORT_ID_DESC,
        "Direction",
        "Ingress",
        "Egress",
        "Unidir",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint16 gport = 0;
    uint32 value = 0;
    uint8 lchip = 0;
    uint8  index = 0;
    ctc_direction_t dir = CTC_INGRESS;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);


    if (CTC_CLI_STR_EQUAL_ENHANCE("ingress", 1))
    {
        dir = CTC_INGRESS;
    }
    else if (CTC_CLI_STR_EQUAL_ENHANCE("egress", 1))
    {
        dir = CTC_EGRESS;
    }

    ret = sys_greatbelt_port_get_sgmii_unidir_en(lchip, gport, dir, &value);
    if(ret != CTC_E_NONE)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }


    ctc_cli_out("%% Port 0x%04x direction %s undir: %s\n", gport, dir?"Egress":"Ingress",value?"enable":"disable");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_port_set_monitor_link,
        ctc_cli_gb_port_set_monitor_link_cmd,
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
    ret = sys_greatbelt_port_set_monitor_link_enable(lchip, value);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_port_get_monitor_link,
        ctc_cli_gb_port_get_monitor_link_cmd,
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
    ret = sys_greatbelt_port_get_monitor_link_enable(lchip, &value);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("%-45s:  %s\n","Link Monitor", value?"enable":"disable");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_port_get_code_error_count,
        ctc_cli_gb_port_get_code_error_count_cmd,
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

    ret = sys_greatbelt_port_get_code_error_count(lchip, gport, &value);
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

int32
ctc_greatbelt_port_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_port_set_scl_key_type_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_port_set_scl_property_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_port_set_vlan_range_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_port_show_port_mac_link_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_port_show_monitor_rlt_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_set_unidir_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_show_unidir_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_set_monitor_link_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_get_monitor_link_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_get_code_error_count_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_port_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_port_set_scl_key_type_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_port_set_scl_property_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_port_set_vlan_range_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_port_show_port_mac_link_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_port_show_monitor_rlt_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_set_unidir_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_show_unidir_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_set_monitor_link_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_get_monitor_link_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_port_get_code_error_count_cmd);

    return CLI_SUCCESS;
}

