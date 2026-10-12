/**
 @file ctc_goldengate_l3if_cli.c

 @date 2010-03-16

 @version v1.0

---file comments----
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_error.h"
#include "ctc_debug.h"

extern int32
sys_goldengate_l3if_show_ecmp_if_info(uint8 lchip, uint16 ecmp_if_id);
CTC_CLI(ctc_cli_gg_l3if_show_ecmp_if,
        ctc_cli_gg_l3if_show_ecmp_if_cmd,
        "show l3if ecmp-if IFID (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_L3IF_STR,
        CTC_CLI_ECMP_L3IF_ID_DESC,
        CTC_CLI_L3IF_ID_RANGE_DESC,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint16 l3if_id = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
    	CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    CTC_CLI_GET_UINT16_RANGE("l3if_id", l3if_id, argv[0], 0, CTC_MAX_UINT16_VALUE);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_l3if_show_ecmp_if_info(lchip, l3if_id);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_goldengate_l3if_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_l3if_show_ecmp_if_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_l3if_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_l3if_show_ecmp_if_cmd);

    return CLI_SUCCESS;
}

