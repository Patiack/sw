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
#include "ctc_scl_cli.h"
#include "ctc_mirror.h"


/****************************************************************************
 *
 * Function
 *
 *****************************************************************************/
extern int32
sys_goldengate_scl_show_status(uint8 lchip);
extern int32
sys_goldengate_scl_show_entry(uint8 lchip, uint8 type, uint32 param, uint32 key_type, uint8 detail);
extern int32
sys_goldengate_scl_show_tunnel_status(uint8 lchip);
extern int32
sys_goldengate_scl_show_tunnel_entry(uint8 lchip, uint8 key_type, uint8 detail);
extern int32
sys_goldengate_scl_show_tcam_alloc_status(uint8 lchip, uint8 fpa_block_id);

CTC_CLI(ctc_cli_gg_scl_show_status,
        ctc_cli_gg_scl_show_status_cmd,
        "show scl status (lchip LCHIP|)",
        "Show",
        CTC_CLI_SCL_STR,
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
    ret = sys_goldengate_scl_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gg_scl_show_tunnel_count,
        ctc_cli_gg_scl_show_tunnel_count_cmd,
        "show tunnel status (lchip LCHIP|)",
        "Show",
        "Tunnel",
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
    ret = sys_goldengate_scl_show_tunnel_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gg_scl_show_tunnel_entry,
        ctc_cli_gg_scl_show_tunnel_entry_cmd,
        "show tunnel entry (lchip LCHIP|)",
        "Show",
        "Tunnel",
        "Entry",
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
    ret = sys_goldengate_scl_show_tunnel_entry(lchip, 0, 0);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}


CTC_CLI(ctc_cli_gg_scl_show_entry_info,
        ctc_cli_gg_scl_show_entry_info_cmd,
        "show scl entry-info \
        (all   | entry ENTRY_ID |group GROUP_ID |priority PRIORITY) \
        (type (tcam-mac|tcam-ipv4 | tcam-ipv4-single | tcam-ipv6 | tcam-vlan | hash-port|  hash-port-cvlan | \
        hash-port-svlan | hash-port-2vlan | hash-port-cvlan-cos| \
        hash-port-svlan-cos | hash-mac | hash-port-mac | hash-ipv4 | hash-port-ipv4 | hash-ipv6| hash-port-ipv6 |hash-l2 | \
        hash-tunnel-ipv4 | hash-tunnel-ipv4-gre | hash-tunnel-ipv4-rpf | hash-tunnel-ipv4-da | hash-nvgre-v4-uc-mode0 | \
        hash-nvgre-v4-uc-mode1 | hash-nvgre-v4-mc-mode0 | hash-nvgre-v4-mode1 | hash-nvgre-v6-uc-mode0 | \
        hash-nvgre-v6-uc-mode1 | hash-nvgre-v6-mc-mode0 | hash-nvgre-v6-mc-mode1 | hash-vxlan-v4-uc-mode0 | \
        hash-vxlan-v4-uc-mode1 | hash-vxlan-v4-mc-mode0 | hash-vxlan-v4-mode1 | hash-vxlan-v6-uc-mode0 | \
        hash-vxlan-v6-uc-mode1 | hash-vxlan-v6-mc-mode0 | hash-vxlan-v6-mc-mode1 | hash-trill-uc | hash-trill-mc) |) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_SCL_STR,
        "Entry info",
        "All entries",
        "By entry",
        CTC_CLI_SCL_ENTRY_ID_VALUE,
        "By group",
        CTC_CLI_SCL_MAX_GROUP_ID_VALUE,
        "By priority",
        CTC_CLI_SCL_GROUP_PRIO_VALUE,
        "By type",
        "SCL Mac-entry",
        "SCL Ipv4-entry",
        "SCL Ipv4-entry(Single width)",
        "SCL Ipv6-entry",
        "SCL Vlan-entry",
        "SCL Hash-port-entry",
        "SCL Hash-port-cvlan-entry",
        "SCL Hash-port-svlan-entry",
        "SCL Hash-port-2vlan-entry",
        "SCL Hash-port-cvlan-cos-entry",
        "SCL Hash-port-svlan-cos-entry",
        "SCL Hash-mac-entry",
        "SCL Hash-port-mac-entry",
        "SCL Hash-ipv4-entry",
        "SCL Hash-port-ipv4-entry",
        "SCL Hash-ipv6-entry",
        "SCL Hash-port-ipv6-entry",
        "SCL Hash-l2-entry",
        "SCL Hash-tunnel-ipv4-entry",
        "SCL Hash-tunnel-ipv4-gre-entry",
        "SCL Hash-tunnel-ipv4-rpf-entry",
        "SCL Hash-tunnel-ipv4-da-entry",
        "SCL Hash-nvgre-v4-uc-mode0",
        "SCL Hash-nvgre-v4-uc-mode1",
        "SCL Hash-nvgre-v4-mc-mode0",
        "SCL Hash-nvgre-v4-mode1",
        "SCL Hash-nvgre-v6-uc-mode0",
        "SCL Hash-nvgre-v6-uc-mode1",
        "SCL Hash-nvgre-v6-mc-mode0",
        "SCL Hash-nvgre-v6-mc-mode1",
        "SCL Hash-vxlan-v4-uc-mode0",
        "SCL Hash-vxlan-v4-uc-mode1",
        "SCL Hash-vxlan-v4-mc-mode0",
        "SCL Hash-vxlan-v4-mode1",
        "SCL Hash-vxlan-v6-uc-mode0",
        "SCL Hash-vxlan-v6-uc-mode1",
        "SCL Hash-vxlan-v6-mc-mode0",
        "SCL Hash-vxlan-v6-mc-mode1",
        "SCL hash-trill-uc",
        "SCL hash-trill-mc",
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
    uint32 key_type = CTC_SCL_KEY_NUM;

    index = CTC_CLI_GET_ARGC_INDEX("all");
    if (INDEX_VALID(index))
    {
        type = 0;
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
        CTC_CLI_GET_UINT32("priority", param, argv[index + 1]);
        type = 3;
    }

    index = CTC_CLI_GET_ARGC_INDEX("type");
    if (INDEX_VALID(index))
    {
        if CLI_CLI_STR_EQUAL("tcam-mac", index + 1)
        {
            key_type = 1; /*SYS_SCL_KEY_TCAM_MAC;*/
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("tcam-ipv4", index + 1)
        {
            key_type = 2; /* SYS_SCL_KEY_TCAM_IPV4; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("tcam-ipv4-single", index + 1)
        {
            key_type = 3; /* SYS_SCL_KEY_TCAM_IPV4_SINGLE; */
        }
        else if CLI_CLI_STR_EQUAL("tcam-ipv6", index + 1)
        {
            key_type = 4; /* SYS_SCL_KEY_TCAM_IPV6; */
        }
        else if CLI_CLI_STR_EQUAL("tcam-vlan", index + 1)
        {
            key_type = 0; /* SYS_SCL_KEY_TCAM_VLAN; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-cvlan", index + 1)
        {
            key_type = 6; /* SYS_SCL_KEY_HASH_PORT_CVLAN; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-svlan", index + 1)
        {
            key_type = 7; /* SYS_SCL_KEY_HASH_PORT_SVLAN; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-2vlan", index + 1)
        {
            key_type = 8; /* SYS_SCL_KEY_HASH_PORT_2VLAN; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-cvlan-cos", index + 1)
        {
            key_type = 9; /* SYS_SCL_KEY_HASH_PORT_CVLAN_COS; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-svlan-cos", index + 1)
        {
            key_type = 10; /* SYS_SCL_KEY_HASH_PORT_SVLAN_COS; */
        }
        else if CLI_CLI_STR_EQUAL("hash-mac", index + 1)
        {
            key_type = 11; /* SYS_SCL_KEY_HASH_MAC; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-mac", index + 1)
        {
            key_type = 12; /* SYS_SCL_KEY_HASH_PORT_MAC; */
        }
        else if CLI_CLI_STR_EQUAL("hash-ipv4", index + 1)
        {
            key_type = 13; /* SYS_SCL_KEY_HASH_IPV4; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-ipv4", index + 1)
        {
            key_type = 14; /* SYS_SCL_KEY_HASH_PORT_IPV4; */
        }
        else if CLI_CLI_STR_EQUAL("hash-ipv6", index + 1)
        {
            key_type = 15; /* SYS_SCL_KEY_HASH_IPV6; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port-ipv6", index + 1)
        {
            key_type = 16; /* SYS_SCL_KEY_HASH_PORT_IPV6; */
        }
        else if CLI_CLI_STR_EQUAL("hash-l2", index + 1)
        {
            key_type = 17; /* SYS_SCL_KEY_HASH_L2; */
        }
        else if CLI_CLI_STR_EQUAL("hash-port", index + 1)
        {
            key_type = 5; /* SYS_SCL_KEY_HASH_PORT; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-tunnel-ipv4", index + 1)
        {
            key_type = 18; /* SYS_SCL_KEY_HASH_TUNNEL_IPV4; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-tunnel-ipv4-gre", index + 1)
        {
            key_type = 19; /* SYS_SCL_KEY_HASH_TUNNEL_IPV4_GRE; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-tunnel-ipv4-rpf", index + 1)
        {
            key_type = 20; /* SYS_SCL_KEY_HASH_TUNNEL_IPV4_RPF; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-tunnel-ipv4-da", index + 1)
        {
            key_type = 21; /* SYS_SCL_KEY_HASH_TUNNEL_IPV4_DA; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v4-uc-mode0", index + 1)
        {
            key_type = 22; /* SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v4-uc-mode1", index + 1)
        {
            key_type = 23; /* SYS_SCL_KEY_HASH_NVGRE_UC_V4_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v4-mc-mode0", index + 1)
        {
            key_type = 24; /* SYS_SCL_KEY_HASH_NVGRE_MC_V4_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v4-mode1", index + 1)
        {
            key_type = 25; /* SYS_SCL_KEY_HASH_NVGRE_V4_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v6-uc-mode0", index + 1)
        {
            key_type = 26; /* SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v6-uc-mode1", index + 1)
        {
            key_type = 27; /* SYS_SCL_KEY_HASH_NVGRE_UC_V6_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v6-mc-mode0", index + 1)
        {
            key_type = 28; /* SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-nvgre-v6-mc-mode1", index + 1)
        {
            key_type = 29; /* SYS_SCL_KEY_HASH_NVGRE_MC_V6_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v4-uc-mode0", index + 1)
        {
            key_type = 30; /* SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v4-uc-mode1", index + 1)
        {
            key_type = 31; /* SYS_SCL_KEY_HASH_VXLAN_UC_V4_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v4-mc-mode0", index + 1)
        {
            key_type = 32; /* SYS_SCL_KEY_HASH_VXLAN_MC_V4_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v4-mode1", index + 1)
        {
            key_type = 33; /* SYS_SCL_KEY_HASH_VXLAN_V4_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v6-uc-mode0", index + 1)
        {
            key_type = 34; /* SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v6-uc-mode1", index + 1)
        {
            key_type = 35; /* SYS_SCL_KEY_HASH_VXLAN_UC_V6_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v6-mc-mode0", index + 1)
        {
            key_type = 36; /* SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE0; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-vxlan-v6-mc-mode1", index + 1)
        {
            key_type = 37; /* SYS_SCL_KEY_HASH_VXLAN_MC_V6_MODE1; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-trill-uc", index + 1)
        {
            key_type = 38; /* SYS_SCL_KEY_HASH_TRILL_UC; */
        }
        else if CTC_CLI_STR_EQUAL_ENHANCE("hash-trill-mc", index + 1)
        {
            key_type = 40; /* SYS_SCL_KEY_HASH_TRILL_MC; */
        }
    }
    else /*all type*/
    {
        key_type = 45; /* SYS_SCL_KEY_NUM; */
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
    ret = sys_goldengate_scl_show_entry(lchip, type, param, key_type, detail);
    if (ret < 0)
    {
        ctc_cli_out("ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_scl_show_block_status,
        ctc_cli_gg_scl_show_block_status_cmd,
        "show scl block-id BLOCK_ID status (lchip LCHIP|)",
        "Show",
        CTC_CLI_SCL_STR,
        "block id",
        "scl block id",
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
    ret = sys_goldengate_scl_show_tcam_alloc_status(lchip, block_id);

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

int32
ctc_goldengate_scl_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_entry_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_tunnel_count_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_tunnel_entry_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_scl_show_block_status_cmd);

    return 0;
}

int32
ctc_goldengate_scl_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_entry_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_tunnel_count_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_scl_show_tunnel_entry_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_scl_show_block_status_cmd);

    return 0;
}

