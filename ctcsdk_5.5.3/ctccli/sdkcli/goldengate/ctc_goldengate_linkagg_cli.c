
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
#include "ctc_linkagg_cli.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"

extern int32
sys_goldengate_linkagg_show_all_member(uint8 lchip, uint8 tid);

int32
sys_goldengate_linkagg_create_channel_agg(uint8 lchip, ctc_linkagg_group_t* p_linkagg_grp);
int32
sys_goldengate_linkagg_destroy_channel_agg(uint8 lchip, uint8 tid);
int32
sys_goldengate_linkagg_add_channel(uint8 lchip, uint8 tid, uint16 gport);
int32
sys_goldengate_linkagg_remove_channel(uint8 lchip, uint8 tid, uint16 gport);

CTC_CLI(ctc_cli_gg_linkagg_show_detail_member_port,
        ctc_cli_gg_linkagg_show_detail_member_port_cmd,
        "show linkagg AGG_ID member-ports detail (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_LINKAGG_M_STR,
        CTC_CLI_LINKAGG_ID_DESC,
        "Member ports of linkagg group",
        "all member include pad member",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 tid = 0;
    uint8 index = 0;

    CTC_CLI_GET_INTEGER("linkagg id", tid, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_linkagg_show_all_member(lchip, tid);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_linkagg_channel_agg_group,
        ctc_cli_gg_linkagg_channel_agg_group_cmd,
        "linkagg (create|remove) linkagg AGG_ID channel-agg (lchip LCHIP|)",
        CTC_CLI_LINKAGG_M_STR,
        "Create linkagg group",
        "Remove linkagg group",
        CTC_CLI_LINKAGG_DESC,
        "Linkagg id value",
        "Channel aggregation",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 index = 0;
    uint8 lchip = 0;
    ctc_linkagg_group_t linkagg_grp;

    sal_memset(&linkagg_grp, 0, sizeof(ctc_linkagg_group_t));
    CTC_CLI_GET_UINT8("linkagg id", linkagg_grp.tid, argv[1]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    index = CTC_CLI_GET_ARGC_INDEX("create");
    if (0xFF != index)
    {
        ret = sys_goldengate_linkagg_create_channel_agg(lchip, &linkagg_grp);
    }
    else
    {
        ret = sys_goldengate_linkagg_destroy_channel_agg(lchip, linkagg_grp.tid);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}


CTC_CLI(ctc_cli_gg_linkagg_channel_agg_member,
        ctc_cli_gg_linkagg_channel_agg_member_cmd,
        "linkagg AGG_ID (add | remove) member-port GPHYPORT_ID channel-agg (lchip LCHIP|)",
        CTC_CLI_LINKAGG_M_STR,
        CTC_CLI_LINKAGG_ID_DESC,
        "Add member port to linkagg group",
        "Remove member port from linkagg group",
        "Member-port, global port",
        CTC_CLI_GPHYPORT_ID_DESC,
        "Channel aggregation",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 tid = CTC_MAX_LINKAGG_GROUP_NUM;
    uint16 gport = 0;
    uint8 index = 0;
    uint8 lchip = 0;

    CTC_CLI_GET_UINT8("linkagg id", tid, argv[0]);
    CTC_CLI_GET_UINT16("gport", gport, argv[2]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    index = CTC_CLI_GET_ARGC_INDEX("add");
    if (0xFF != index)
    {
        ret = sys_goldengate_linkagg_add_channel(lchip, tid, gport);
    }
    else
    {
        ret = sys_goldengate_linkagg_remove_channel(lchip, tid, gport);
    }


    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_goldengate_linkagg_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_linkagg_show_detail_member_port_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_linkagg_channel_agg_group_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_linkagg_channel_agg_member_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_linkagg_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_linkagg_show_detail_member_port_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_linkagg_channel_agg_group_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_linkagg_channel_agg_member_cmd);

    return CLI_SUCCESS;
}

