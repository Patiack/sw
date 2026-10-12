
/**
 @file ctc_greatbel_vlan_cli.c

 @date 2011-11-25

 @version v2.0

---file comments----
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_vlan_cli.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"

extern int32
sys_goldengate_vlan_get_internal_property(uint8 lchip, uint16 vlan_id, uint32 vlan_prop, uint32* value);

extern int32
sys_goldengate_vlan_show_default_vlan_mapping(uint8 lchip, uint16 gport);

extern int32
sys_goldengate_vlan_show_default_vlan_class(uint8 lchip);

extern int32
sys_goldengate_scl_show_vlan_mapping_entry(uint8 lchip, uint8 key_type, uint8 detail);

extern int32
sys_goldengate_scl_show_vlan_class_entry(uint8 lchip, uint8 key_type, uint8 detail);


CTC_CLI(ctc_cli_gg_vlan_show_vlan_l3if_id,
        ctc_cli_gg_vlan_show_vlan_l3if_id_cmd,
        "show vlan VLAN_ID l3if-id (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_VLAN_DESC,
        CTC_CLI_VLAN_RANGE_DESC,
        "L3if id",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint16 vlan_id;
    uint32 l3if_id = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    
    CTC_CLI_GET_UINT16("vlan id", vlan_id, argv[0]);

    /* SYS_VLAN_PROP_INTERFACE_ID */
    ret  = sys_goldengate_vlan_get_internal_property(lchip, vlan_id, 1, &l3if_id);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("%-24s%s\n", "Property", "Value");
    ctc_cli_out("---------------------------------------\n");
    if (0 == l3if_id)
    {
        ctc_cli_out("%-24s%s\n", "l3if-id", "none");
    }
    else
    {
        ctc_cli_out("%-24s%u\n", "l3if-id", l3if_id);
    }

    return CTC_E_NONE;
}

CTC_CLI(ctc_cli_gg_vlan_show_vlan_mapping_default_entry,
        ctc_cli_gg_vlan_show_vlan_mapping_default_entry_cmd,
        "show vlan mapping default info port GPORT_ID (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_VLAN_M_STR,
        "Vlan mapping",
        "Default",
        "Vlan mapping default information",
        CTC_CLI_GPORT_DESC,
        CTC_CLI_GPORT_ID_WITHOUT_LINKAGG_DESC,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret;
    uint16 gport;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    CTC_CLI_GET_UINT16_RANGE("gport", gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_vlan_show_default_vlan_mapping(lchip, gport);

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_vlan_show_vlan_class_default_entry,
        ctc_cli_gg_vlan_show_vlan_class_default_entry_cmd,
        "show vlan classifier default info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_VLAN_M_STR,
        "Vlan classification",
        "Default",
        "Vlan classification default information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_vlan_show_default_vlan_class(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
    }

    return ret;
}

extern int32
sys_goldengate_vlan_db_dump_all(uint8 lchip);

CTC_CLI(ctc_cli_gg_vlan_show_vlan_profile,
        ctc_cli_gg_vlan_show_vlan_profile_cmd,
        "show vlan profile all (lchip LCHIP|)",
        "Do show",
        CTC_CLI_VLAN_M_STR,
        "Vlan profile",
        "All",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_vlan_db_dump_all(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% ret=%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}

CTC_CLI(ctc_cli_gg_vlan_show_vlan_class,
        ctc_cli_gg_vlan_show_vlan_class_cmd,
        "show vlan classifier (mac |ipv4 |ipv6) entry (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_VLAN_M_STR,
        "Vlan classification",
        "Mac entry",
        "Ipv4 entry",
        "Ipv6 entry",
        "Entry",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 type = 0;

    index = CTC_CLI_GET_ARGC_INDEX("mac");
    if (0xFF != index)
    {
        type = 1; /* SYS_SCL_KEY_TCAM_MAC */
    }

    index = CTC_CLI_GET_ARGC_INDEX("ipv4");
    if (0xFF != index)
    {
        type = 3; /* SYS_SCL_KEY_TCAM_IPV4_SINGLE */
    }

    index = CTC_CLI_GET_ARGC_INDEX("ipv6");
    if (0xFF != index)
    {
        type = 4; /* SYS_SCL_KEY_TCAM_IPV6 */
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

     /* key_type = 45 : SYS_SCL_KEY_NUM; */
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_scl_show_vlan_class_entry(lchip, type, 0);

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_vlan_show_vlan_mapping,
        ctc_cli_gg_vlan_show_vlan_mapping_cmd,
        "show vlan mapping entry (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_VLAN_M_STR,
        "Vlan mapping",
        "Entry",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

     /* key_type = 45 : SYS_SCL_KEY_NUM; */
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_scl_show_vlan_mapping_entry(lchip, 45, 0);

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_goldengate_vlan_cli_init(void)
{

    install_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_l3if_id_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_mapping_default_entry_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_class_default_entry_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_profile_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_mapping_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_class_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_vlan_cli_deinit(void)
{

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_l3if_id_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_mapping_default_entry_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_class_default_entry_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_profile_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_mapping_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_vlan_show_vlan_class_cmd);

    return CLI_SUCCESS;
}

