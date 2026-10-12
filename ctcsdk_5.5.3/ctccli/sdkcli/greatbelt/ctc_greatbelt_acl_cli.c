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
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_acl_cli.h"
#include "ctc_mirror.h"

#include "sys_greatbelt_acl.h"

/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/

CTC_CLI(ctc_cli_gb_acl_show_entry_info,
        ctc_cli_gb_acl_show_entry_info_cmd,
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
    ret = sys_greatbelt_acl_show_entry(lchip, type, param, key_type, detail);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_acl_show_status,
        ctc_cli_gb_acl_show_status_cmd,
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
    ret = sys_greatbelt_acl_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gb_acl_set_global_cfg,
        ctc_cli_gb_acl_set_global_cfg_cmd,
        "acl global-cfg (merge | in-use-mapped-vlan | trill-use-ipv6| arp-use-ipv6 | hash-acl-en |non-ipv4-mpls-use-ipv6|\
    in-port-service-acl-en | in-vlan-service-acl-en | out-port-service-acl-en| out-vlan-service-acl-en |\
    hash-mac-key-flag | hash-ipv4-key-flag | priority-bitmap-of-stats) value VALUE (lchip LCHIP|)",
        CTC_CLI_ACL_STR,
        "Global configuration",
        "Merge mac to ipv4 key",
        "Ingress use mapped vlan instead of packet vlan",
        "Trill packet use ipv6 key",
        "Arp packet use ipv6 key",
        "Hash acl enable",
        "Non ipv4 mpls packet use ipv6 key",
        "Ingress service acl enable on port (bitmap 0~15)",
        "Ingress service acl enable on vlan (bitmap 0~15)",
        "Egress service acl enable on port (bitmap 0~15)",
        "Egress service acl enable on vlan (bitmap 0~15)",
        "Hash mac key flag",
        "Hash ipv4 key flag",
        "Priority bitmap of stats",
        "Property value",
        "Value",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0xFF;
    sys_acl_global_cfg_t cfg;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    sal_memset(&cfg, 0, sizeof(cfg));
    CTC_CLI_GET_UINT32("Property_value", cfg.value, argv[1]);

    if CLI_CLI_STR_EQUAL("merge", 0)
    {
        cfg.flag.merge_mac_ip = 1;
    }
    else if CLI_CLI_STR_EQUAL("in-use-mapped-vlan", 0)
    {
        cfg.flag.ingress_use_mapped_vlan = 1;
    }
    else if CLI_CLI_STR_EQUAL("trill-use-ipv6", 0)
    {
        cfg.flag.trill_use_ipv6 = 1;
    }
    else if CLI_CLI_STR_EQUAL("arp-use-ipv6", 0)
    {
        cfg.flag.arp_use_ipv6 = 1;
    }
    else if CLI_CLI_STR_EQUAL("hash-acl-en", 0)
    {
        cfg.flag.hash_acl_en  = 1;
    }
    else if CLI_CLI_STR_EQUAL("non-ipv4-mpls-use-ipv6", 0)
    {
        cfg.flag.non_ipv4_mpls_use_ipv6 = 1;
    }
    else if CLI_CLI_STR_EQUAL("in-port-service-acl-en", 0)
    {
        cfg.flag.ingress_port_service_acl_en = 1;
    }
    else if CLI_CLI_STR_EQUAL("in-vlan-service-acl-en", 0)
    {
        cfg.flag.ingress_vlan_service_acl_en = 1;
    }
    else if CLI_CLI_STR_EQUAL("out-port-service-acl-en", 0)
    {
        cfg.flag.egress_port_service_acl_en = 1;
    }
    else if CLI_CLI_STR_EQUAL("out-vlan-service-acl-en", 0)
    {
        cfg.flag.egress_vlan_service_acl_en = 1;
    }
    else if CLI_CLI_STR_EQUAL("hash-mac-key-flag", 0)
    {
        cfg.flag.hash_mac_key_flag          = 1;
    }
    else if CLI_CLI_STR_EQUAL("hash-ipv4-key-flag", 0)
    {
        cfg.flag.hash_ipv4_key_flag         = 1;
    }
    else if CLI_CLI_STR_EQUAL("priority-bitmap-of-stats", 0)
    {
        cfg.flag.priority_bitmap_of_stats   = 1;
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_acl_set_global_cfg(lchip, &cfg);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_acl_show_tcp_flags,
        ctc_cli_gb_acl_show_tcp_flags_cmd,
        "show acl (tcp-flags|port-range) (lchip LCHIP|)",
        "Show",
        CTC_CLI_ACL_STR,
        "Tcp flags",
        "Layer4 port range",
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
        ret = sys_greatbelt_acl_show_tcp_flags(lchip);
    }
    else if (CLI_CLI_STR_EQUAL("port-range", 0))
    {
        ret = sys_greatbelt_acl_show_port_range(lchip);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_greatbelt_acl_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_acl_show_entry_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_acl_show_status_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_acl_set_global_cfg_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_acl_show_tcp_flags_cmd);

    return 0;
}

int32
ctc_greatbelt_acl_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_acl_show_entry_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_acl_show_status_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_acl_set_global_cfg_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_acl_show_tcp_flags_cmd);

    return 0;
}

