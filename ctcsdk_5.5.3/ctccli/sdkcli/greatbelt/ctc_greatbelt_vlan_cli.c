
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
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"
#include "ctc_cli_common.h"
#include "sys_greatbelt_vlan.h"
#include "sys_greatbelt_vlan_mapping.h"
#include "sys_greatbelt_vlan_classification.h"

CTC_CLI(ctc_cli_gb_vlan_show_vlan_l3if_id,
        ctc_cli_gb_vlan_show_vlan_l3if_id_cmd,
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
    CTC_CLI_GET_UINT16("vlan id", vlan_id, argv[0]);
    /* SYS_VLAN_PROP_INTERFACE_ID */
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret  = sys_greatbelt_vlan_get_internal_property(lchip, vlan_id, SYS_VLAN_PROP_INTERFACE_ID, &l3if_id);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("Property                Value\n");
    ctc_cli_out("---------------------------------------\n");
    if (0 == l3if_id)
    {
        ctc_cli_out("l3if-id                 none\n", l3if_id);
    }
    else
    {
        ctc_cli_out("l3if-id                 %d\n", l3if_id);
    }
    return CTC_E_NONE;
}

CTC_CLI(ctc_cli_gb_vlan_show_vlan_mapping_default_entry,
        ctc_cli_gb_vlan_show_vlan_mapping_default_entry_cmd,
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
    ret = sys_greatbelt_vlan_show_default_vlan_mapping(lchip, gport);

    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_vlan_show_vlan_class_default_entry,
        ctc_cli_gb_vlan_show_vlan_class_default_entry_cmd,
        "show vlan classifier default info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_VLAN_M_STR,
        "Vlan classification",
        "Default",
        "Vlan mapping default information",
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

    ret = sys_greatbelt_vlan_show_default_vlan_class(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
    }

    return ret;
}

extern int32
sys_greatbelt_vlan_db_dump_all(uint8 lchip);

CTC_CLI(ctc_cli_gb_vlan_show_vlan_profile,
        ctc_cli_gb_vlan_show_vlan_profile_cmd,
        "show vprofile all (lchip LCHIP|)",
        "Do show",
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
    ret = sys_greatbelt_vlan_db_dump_all(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% ret=%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;

}

int32
ctc_greatbelt_vlan_cli_init(void)
{

    install_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_l3if_id_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_mapping_default_entry_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_class_default_entry_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_profile_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_vlan_cli_deinit(void)
{

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_l3if_id_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_mapping_default_entry_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_class_default_entry_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_vlan_show_vlan_profile_cmd);

    return CLI_SUCCESS;
}

