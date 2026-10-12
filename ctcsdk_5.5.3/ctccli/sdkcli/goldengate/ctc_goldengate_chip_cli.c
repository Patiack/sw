/**
 @file ctc_goldengate_chip_cli.c

 @date 2012-3-20

 @version v2.0

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
#include "ctc_chip.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_goldengate_chip_cli.h"
#include "dal.h"




extern int32
sys_goldengate_datapath_show_status(uint8 lchip);
extern int32
sys_goldengate_datapath_show_info(uint8 lchip, uint8 type, uint32 start, uint32 end, uint8 is_all);
extern int32
sys_goldengate_chip_show_status(void);

extern int32
sys_goldengate_global_set_oversub_pdu(uint8 lchip, mac_addr_t macda, mac_addr_t macda_mask, uint16 eth_type, uint16 eth_type_mask, uint8 with_vlan);

CTC_CLI(ctc_cli_gg_datapath_show_status,
        ctc_cli_gg_datapath_show_status_cmd,
        "show datapath status ",
        CTC_CLI_SHOW_STR,
        "Datapath module",
        "Status")
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_datapath_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_datapath_show_info,
        ctc_cli_gg_datapath_show_info_cmd,
        "show datapath info (hss|serdes|clktree|calendar) (((start-idx SIDX) (end-idx EIDX))|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Datapath module",
        "Information",
        "Hss",
        "SerDes",
        "Clock Tree",
        "Calendar",
        "Start index",
        "Index",
        "End index",
        "Index",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 index = 0;
    uint32 start_idx = 0;
    uint32 end_idx = 0;
    uint8 is_all = 1;
    uint8 type = 0;

    index = CTC_CLI_GET_ARGC_INDEX("hss");
    if(index != 0xFF)
    {
         type = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("serdes");
    if(index != 0xFF)
    {
         type = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("clktree");
    if(index != 0xFF)
    {
         type = 2;
    }
    index = CTC_CLI_GET_ARGC_INDEX("calendar");
    if(index != 0xFF)
    {
         type = 3;
    }
    index = CTC_CLI_GET_ARGC_INDEX("start-idx");
    if(index != 0xFF)
    {
       CTC_CLI_GET_UINT32("start-idx", start_idx, argv[index+1]);
       is_all = 0;
    }

    index = CTC_CLI_GET_ARGC_INDEX("end-idx");
    if(index != 0xFF)
    {
       CTC_CLI_GET_UINT32("end-idx", end_idx, argv[index+1]);
       is_all = 0;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_datapath_show_info(lchip, type, start_idx, end_idx, is_all);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


extern dal_op_t g_dal_op;

uint8 g_gg_debug = 0;

CTC_CLI(ctc_cli_gg_debug,
        ctc_cli_gg_debug_cmd,
        "debug driver (on|off)",
        "debug",
        "driver",
        "on",
        "off")
{
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("on");
    if(0xFF != index)
    {
        g_gg_debug = 1;
    }
    else
    {
        g_gg_debug = 0;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_show_chip_info,
        ctc_cli_gg_chip_show_chip_info_cmd,
        "show chip status",
        "show",
        "chip module",
        "chip information")
{
    int32 ret = CLI_SUCCESS;

    ret = sys_goldengate_chip_show_status();
    if (ret)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_chip_set_global_oversub_pdu,
        ctc_cli_gg_chip_set_global_oversub_pdu_cmd,
        "chip set oversub-pdu ({macda MACDA MASK |eth-type ETH_TYPE MASK |with-vlan} |disable) (lchip LCHIP|)",
        "chip module",
        "set operate",
        "oversub-pdu",
        "MAC destination address",
        CTC_CLI_MAC_FORMAT,
        CTC_CLI_MAC_FORMAT,
        "Ether type",
        CTC_CLI_ETHER_TYPE_VALUE,
        CTC_CLI_ETHER_TYPE_MASK,
        "Packet with one vlan tag",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    mac_addr_t macda = { 0 };
    mac_addr_t macda_mask = { 0 };
    uint16 eth_type = 0;
    uint16 eth_type_mask = 0;
    uint8 with_vlan = 0;


    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    index = CTC_CLI_GET_ARGC_INDEX("macda");
    if (index != 0xFF)
    {
        CTC_CLI_GET_MAC_ADDRESS("mac-da", macda, argv[index + 1]);
        CTC_CLI_GET_MAC_ADDRESS("mac-da-mask", macda_mask, argv[index + 2]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("eth-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16("eth-type", eth_type, argv[index + 1]);
        CTC_CLI_GET_UINT16("eth-type-mask", eth_type_mask, argv[index + 2]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("with-vlan");
    if (index != 0xFF)
    {
        with_vlan = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("disable");
    if (index != 0xFF)
    {
        sal_memset(macda_mask, 0, sizeof(mac_addr_t));
        eth_type_mask = 0;
    }

    ret = sys_goldengate_global_set_oversub_pdu(lchip, macda, macda_mask, eth_type, eth_type_mask, with_vlan);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_chip_set_ipmc_config,
        ctc_cli_gg_chip_set_ipmc_config_cmd,
        "chip global-cfg ipmc-property vrf-mode MODE",
        "Chip module",
        "Global config",
        "Ipmc property",
        "Vrf mode",
        "If set, vrfid in IPMC key is svlan id")
{
    int32 ret = CLI_SUCCESS;
    ctc_global_ipmc_property_t property;

    sal_memset(&property, 0, sizeof(ctc_global_ipmc_property_t));
    CTC_CLI_GET_UINT8("value", property.vrf_mode, argv[0]);

    if(g_ctcs_api_en)
    {
        ret = ctcs_global_ctl_set(g_api_lchip, CTC_GLOBAL_IPMC_PROPERTY, &property);
    }
    else
    {
        ret = ctc_global_ctl_set(CTC_GLOBAL_IPMC_PROPERTY, &property);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}

CTC_CLI(ctc_cli_gg_chip_show_ipmc_config,
        ctc_cli_gg_chip_show_ipmc_config_cmd,
        "show chip global-cfg ipmc-property",
        "Show",
        "Chip module",
        "Global config",
        "Ipmc property")
{
    int32 ret = CLI_SUCCESS;
    ctc_global_ipmc_property_t property;

    sal_memset(&property, 0, sizeof(ctc_global_ipmc_property_t));
    if(g_ctcs_api_en)
    {
         ret = ctcs_global_ctl_get(g_api_lchip, CTC_GLOBAL_IPMC_PROPERTY, &property);
    }
    else
    {
        ret = ctc_global_ctl_get(CTC_GLOBAL_IPMC_PROPERTY, &property);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("Show chip global-cfg ipmc info\n");
    ctc_cli_out("==============================\n");
    ctc_cli_out("vrf mode : %d\n", property.vrf_mode);
    ctc_cli_out("==============================\n");

    return ret;
}


int32
ctc_goldengate_chip_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_datapath_show_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_datapath_show_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_debug_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_show_chip_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_set_ipmc_config_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_chip_show_ipmc_config_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_chip_set_global_oversub_pdu_cmd);

    return 0;
}

int32
ctc_goldengate_chip_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_datapath_show_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_datapath_show_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_debug_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_show_chip_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_set_ipmc_config_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_chip_show_ipmc_config_cmd);

    return 0;
}

