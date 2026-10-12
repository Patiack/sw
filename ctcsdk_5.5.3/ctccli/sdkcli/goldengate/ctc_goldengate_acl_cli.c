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
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_acl_cli.h"
#include "ctc_mirror.h"



extern int32
sys_goldengate_acl_show_entry(uint8 lchip, uint8 type, uint32 param, ctc_acl_key_type_t key_type, uint8 detail);

extern int32
sys_goldengate_acl_show_status(uint8 lchip);

extern int32
sys_goldengate_acl_set_global_cfg(uint8 lchip, uint8 property, uint32 value);

extern int32
sys_goldengate_acl_show_tcp_flags(uint8 lchip);

extern int32
sys_goldengate_acl_show_port_range(uint8 lchip);

extern int32
sys_goldengate_acl_show_pkt_len_range(uint8 lchip);

extern int32
sys_goldengate_acl_set_tcam_mode(uint8 lchip, uint8 mode);

extern int32
sys_goldengate_dma_set_tcam_interval(uint8 lchip, uint32 interval);

extern int32
sys_goldengate_acl_show_group(uint8 lchip, uint8 type);

extern int32
sys_goldengate_acl_set_pkt_length_range(uint8 lchip, uint8 num, uint32* p_array);

extern int32
sys_goldengate_acl_set_pkt_length_range_en(uint8 lchip, uint8 enable);

extern int32
sys_goldengate_acl_show_tcam_alloc_status(uint8 lchip, uint8 fpa_block_id);

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

CTC_CLI(ctc_cli_gg_acl_show_entry_info,
        ctc_cli_gg_acl_show_entry_info_cmd,
        "show acl entry-info \
    (all (divide-by-group|)  | entry ENTRY_ID |group GROUP_ID |priority PRIORITY ) \
    (type (mac|ipv4|ipv6|mpls)|) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_ACL_STR,
        "Entry info",
        "All entries",
        "All entrise divide by group",
        "By entry",
        CTC_CLI_ACL_ENTRY_ID_VALUE,
        "By group",
        CTC_CLI_ACL_B_GROUP_ID_VALUE,
        "By group priority",
        CTC_CLI_ACL_GROUP_PRIO_VALUE,
        "By type",
        "ACL Mac-entry",
        "ACL Ipv4-entry",
        "ACL Ipv6-entry",
        "ACL Mpls-entry",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0xFF;
    uint8 type = 0;
    uint8 detail = 0;
    uint32 param = 0;
    ctc_acl_key_type_t key_type = CTC_ACL_KEY_NUM;

    index = CTC_CLI_GET_ARGC_INDEX("all");
    if (INDEX_VALID(index))
    {
        type = 0;

        index = CTC_CLI_GET_ARGC_INDEX("divide-by-group");
        if (INDEX_VALID(index))
        { /*0 = by priority. 1 = by group*/
            param = 1;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("entry");
    if (INDEX_VALID(index))
    {
        type = 1;
        CTC_CLI_GET_UINT32("entry id", param, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("group");
    if (INDEX_VALID(index))
    {
        type = 2;
        CTC_CLI_GET_UINT32("group id", param, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("priority");
    if (INDEX_VALID(index))
    {
        type = 3;
        CTC_CLI_GET_UINT32("priority", param, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("type");
    if (INDEX_VALID(index))
    {
        if CLI_CLI_STR_EQUAL("mac", index + 1)
        {
            key_type = CTC_ACL_KEY_MAC;
        }
        else if CLI_CLI_STR_EQUAL("ipv4", index + 1)
        {
            key_type = CTC_ACL_KEY_IPV4;
        }
        else if CLI_CLI_STR_EQUAL("ipv6", index + 1)
        {
            key_type = CTC_ACL_KEY_IPV6;
        }
        else if CLI_CLI_STR_EQUAL("mpls", index + 1)
        {
            key_type = CTC_ACL_KEY_MPLS;
        }
    }
    else /*all type*/
    {
        key_type = CTC_ACL_KEY_NUM;
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (INDEX_VALID(index))
    {
        detail = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_acl_show_entry(lchip, type, param, key_type, detail);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_acl_show_group_info,
        ctc_cli_gg_acl_show_group_info_cmd,
        "show acl group-info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_ACL_STR,
        "Group info")
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0xFF;
    uint8 type = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_acl_show_group(lchip, type);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_acl_show_status,
        ctc_cli_gg_acl_show_status_cmd,
        "show acl status (lchip LCHIP|)",
        "Show",
        CTC_CLI_ACL_STR,
        "Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0xFF;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_acl_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gg_acl_set_global_cfg,
        ctc_cli_gg_acl_set_global_cfg_cmd,
        "acl global-cfg (in-port-service-acl-en | in-vlan-service-acl-en |\
        out-port-service-acl-en| out-vlan-service-acl-en | in-vlan-acl-num | out-vlan-acl-num ) value VALUE (lchip LCHIP|)",
        CTC_CLI_ACL_STR,
        "Global configuration",
        "Ingress service acl enable on port (bitmap 0~15)",
        "Ingress service acl enable on vlan (bitmap 0~15)",
        "Egress service acl enable on port (bitmap 0~1)",
        "Egress service acl enable on vlan (bitmap 0~1)",
        "Ingress vlan acl num",
        "Egress vlan acl num",
        "Property value",
        "Value",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0xFF;
    int32 value = 0;
    int32 property = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }


    CTC_CLI_GET_UINT32("Property_value", value, argv[1]);

    /* property in sys_acl_global_property_t */
    if CLI_CLI_STR_EQUAL("in-port-service-acl-en", 0)
    {
        property = 0;  /* SYS_ACL_IGS_PORT_SERVICE_ACL_EN */
    }
    else if CLI_CLI_STR_EQUAL("in-vlan-service-acl-en", 0)
    {
        property = 1;  /* SYS_ACL_IGS_VLAN_SERVICE_ACL_EN */
    }
    else if CLI_CLI_STR_EQUAL("out-port-service-acl-en", 0)
    {
        property = 3;  /* SYS_ACL_EGS_PORT_SERVICE_ACL_EN */
    }
    else if CLI_CLI_STR_EQUAL("out-vlan-service-acl-en", 0)
    {
        property = 4;  /* SYS_ACL_EGS_VLAN_SERVICE_ACL_EN */
    }
    else if CLI_CLI_STR_EQUAL("in-vlan-acl-num", 0)
    {
        property = 2;  /* SYS_ACL_IGS_VLAN_ACL_NUM */
    }
    else if CLI_CLI_STR_EQUAL("out-vlan-acl-num", 0)
    {
        property = 5;  /* SYS_ACL_EGS_VLAN_ACL_NUM */
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_acl_set_global_cfg(lchip, property, value);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_gg_acl_tcam_access_cfg,
        ctc_cli_gg_acl_tcam_access_cfg_cmd,
        "acl global-cfg tcam-access (dma interval INVERTAL|io) (lchip LCHIP|)",
        CTC_CLI_ACL_STR,
        "Global configuration",
        "Tcam access mode",
        "Using Dma",
        "Dma trigger interval",
        "Interval value(unit is us)",
        "Using IO",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 index = 0;
    uint8 access_mode = 0;
    uint32 interval = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("dma");
    if (index != 0xFF)
    {
        access_mode = 1;
        CTC_CLI_GET_UINT32("interval", interval, argv[index + 2]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("io");
    if (index != 0xFF)
    {
        access_mode = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_acl_set_tcam_mode(lchip, access_mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    if (access_mode)
    {
        ret = sys_goldengate_dma_set_tcam_interval(lchip, interval);
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_acl_show_tcp_flags,
        ctc_cli_gg_acl_show_tcp_flags_cmd,
        "show acl (tcp-flags|port-range|pkt-len-range) (lchip LCHIP|)",
        "Show",
        CTC_CLI_ACL_STR,
        "Tcp flags",
        "Layer4 port range",
        "Pakcet Length range",
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
    if (CLI_CLI_STR_EQUAL("tcp-flags", 0))
    {
        ret = sys_goldengate_acl_show_tcp_flags(lchip);
    }
    else if (CLI_CLI_STR_EQUAL("port-range", 0))
    {
        ret = sys_goldengate_acl_show_port_range(lchip);
    }
    else if (CLI_CLI_STR_EQUAL("pkt-len-range", 0))
    {
        ret = sys_goldengate_acl_show_pkt_len_range(lchip);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_gg_acl_enable_pkt_length,
        ctc_cli_gg_acl_enable_pkt_length_cmd,
        "acl global-cfg (pkt-len-range-en ) value VALUE (lchip LCHIP|)",
        CTC_CLI_ACL_STR,
        "Global configuration",
        "Enable packet length range",
        "Property value",
        "Value",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0xFF;
    int32 value = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    CTC_CLI_GET_UINT32("Property_value", value, argv[1]);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_acl_set_pkt_length_range_en(lchip, value);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_acl_show_block_status,
        ctc_cli_gg_acl_show_block_status_cmd,
        "show acl block-id BLOCK_ID status (lchip LCHIP|)",
        "Show",
        CTC_CLI_ACL_STR,
        "block id",
        "Ingress ACL: 0-3, Egress ACL: 4, Pbr: 5",
        "status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 block_id = 0;

    CTC_CLI_GET_UINT8("block-id", block_id, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    ret = sys_goldengate_acl_show_tcam_alloc_status(lchip, block_id);

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

int32
ctc_goldengate_acl_cli_init(void)
{
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_show_block_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_entry_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_group_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_status_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_set_global_cfg_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_tcp_flags_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_tcam_access_cfg_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_enable_pkt_length_cmd);

    return 0;
}

int32
ctc_goldengate_acl_cli_deinit(void)
{
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_show_block_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_entry_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_group_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_status_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_set_global_cfg_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_acl_show_tcp_flags_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_tcam_access_cfg_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_acl_enable_pkt_length_cmd);

    return 0;
}

