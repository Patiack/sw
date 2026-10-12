
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
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"
#include "sys_greatbelt_linkagg.h"
#include "ctc_greatbelt_linkagg_cli.h"

extern sys_linkagg_master_t* p_gb_linkagg_master[CTC_MAX_LOCAL_CHIP_NUM];

CTC_CLI(ctc_cli_gb_linkagg_show_detail_member_port,
        ctc_cli_gb_linkagg_show_detail_member_port_cmd,
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
    uint8 index = 0;
    uint8 idx = 0;
    uint8 tid = 0;
    uint16 gport = 0;
    sys_linkagg_port_t* p_linkagg_port;
    uint16 mem_num = 0;
    uint8 mode_index = 0;
    char* mode;

    CTC_CLI_GET_INTEGER("linkagg id", tid, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_linkagg_get_max_mem_num(lchip, &mem_num);
    if(ret)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }
    ret = sys_greatbelt_linkagg_show_all_member(lchip, tid, &p_linkagg_port, &mode_index);

    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    switch (mode_index)
    {
        case CTC_LINKAGG_MODE_STATIC:
            mode = "static";
            break;

        case CTC_LINKAGG_MODE_STATIC_FAILOVER:
            mode = "static-failover";
            break;

        case CTC_LINKAGG_MODE_RR:
            mode = "round-robin";
            break;

        case CTC_LINKAGG_MODE_DLB:
            mode = "dynamic";
            break;

        default:
            ctc_cli_out("%% error linkagg mode! \n");
            return CLI_ERROR;
    }

    ctc_cli_out("tid   : %d\n",tid);
    ctc_cli_out("mode  : %s\n",mode);

    ctc_cli_out("%-5s  %-12s %-6s %-6s\n","No.","Member-Port","Valid","Pad");
    ctc_cli_out("--------------------------------\n");

    for (idx = 0; idx < mem_num; idx++)
    {
        gport = p_linkagg_port[idx].gport;
        ctc_cli_out("%-5d  0x%-10x %-6s %-6s\n",idx,gport,(p_linkagg_port[idx].valid)? "TRUE":"FALSE",
            (p_linkagg_port[idx].pad_flag)? "TRUE":"FALSE");
    }

    return ret;
}


int32
ctc_greatbelt_linkagg_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_linkagg_show_detail_member_port_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_linkagg_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_linkagg_show_detail_member_port_cmd);

    return CLI_SUCCESS;
}

