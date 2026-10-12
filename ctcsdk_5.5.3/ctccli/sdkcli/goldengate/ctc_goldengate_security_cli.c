/**
 @file ctc_greatbel_security_cli.c

 @date 2011-11-25

 @version v2.0

---file comments----
*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "ctc_security_cli.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_debug.h"

extern int32
sys_goldengate_mac_limit_set_mode(uint8 lchip, uint8 mode);

CTC_CLI(ctc_cli_gg_security_mac_limit_set_mode,
        ctc_cli_gg_security_mac_limit_set_mode_cmd,
        "security learn-limit mode (hw |sw) (lchip LCHIP|)",
        CTC_CLI_SECURITY_STR,
        "Learn limit",
        "Learn limit mode",
        "Hardware mode",
        "Software mode",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 index = 0;
    uint8 mode = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("hw");
    if(0xFF != index)
    {
        mode = 0;
    }
    else
    {
        mode = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_mac_limit_set_mode(lchip, mode);
    if (ret < 0)
    {
        ctc_cli_out("%% ret =%d, %s\n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_goldengate_security_cli_init(void)
{
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_security_mac_limit_set_mode_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_security_cli_deinit(void)
{
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_security_mac_limit_set_mode_cmd);

    return CLI_SUCCESS;
}

