/**
 @file ctc_nexthop_cli.c

 @date 2009-11-30

 @version v2.0

 The file apply clis of port module
*/

#include "ctc_api.h"
#include "ctcs_api.h"
#include "sal.h"
#include "ctc_cli.h"
#include "ctc_l2.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_nexthop_cli.h"
#include "ctc_cli_common.h"

enum sys_greatbelt_nh_type_e
{
    CLI_NH_TYPE_NULL,
    CLI_NH_TYPE_MCAST,
    CLI_NH_TYPE_BRGUC,
    CLI_NH_TYPE_IPUC,
    CLI_NH_TYPE_MPLS,
    CLI_NH_TYPE_ECMP, /*For IPUC, MPLS, etc*/
    CLI_NH_TYPE_DROP,
    CLI_NH_TYPE_TOCPU,
    CLI_NH_TYPE_UNROV,
    CLI_NH_TYPE_ILOOP,
    CLI_NH_TYPE_ELOOP,
    CLI_NH_TYPE_RSPAN,
    CLI_NH_TYPE_MISC,
    CLI_NH_TYPE_IP_TUNNEL, /*Added by IP Tunnel*/
    CLI_NH_TYPE_MAX
};
typedef enum sys_greatbelt_nh_type_e sys_greatbelt_nh_type_t;

extern int32
sys_greatbelt_nh_dump_all(uint8 lchip, sys_greatbelt_nh_type_t nh_type, bool detail);
extern int32
sys_greatbelt_nh_display_current_global_sram_info(uint8 lchip);
int32
sys_greatbelt_nh_dump_mpls_tunnel(uint8 lchip,uint16 tunnel_id, bool detail);
extern int32
sys_greatbelt_nh_dump(uint8 lchip, uint32 nhid, bool detail);
extern int32
sys_greatbelt_nh_set_ecmp_mode(uint8 lchip,uint8 enable);
extern int32
sys_greatbelt_nh_set_dsfwd_mode(uint8 lchip, uint8 have_dsfwd);

CTC_CLI(ctc_cli_gb_nh_show_nexthop_by_nhid_type,
        ctc_cli_gb_nh_show_nexthop_by_nhid_type_cmd,
        "show nexthop NHID (detail |) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_NH_M_STR,
        CTC_CLI_NH_ID_STR,
        "Display detail information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint32 nhid;
    uint8 index = 0;
    uint8 lchip = 0;
    int32 ret = CLI_ERROR;
    bool detail = FALSE;

    CTC_CLI_GET_UINT32("NexthopID", nhid, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (0xFF != index)
    {
        detail = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_dump(lchip, nhid, detail);
    if (ret)
    {
        ctc_cli_out("%% Dump nexthop fail\n");
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_nh_show_nexthop_all_by_type,
        ctc_cli_gb_nh_show_nexthop_all_by_type_cmd,
        "show nexthop all ((mcast|brguc|ipuc|ecmp|mpls|iloop|rspan|ip-tunnel|misc)|) (detail |) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_NH_M_STR,
        "All nexthop",
        "Multicast nexthop",
        "Bridge unicast nexthop",
        "Ipuc nexthop",
        "ECMP nexthop",
        "MPLS nexthop",
        "ILoop nexthop",
        "RSPAN nexthop",
        "Ip tunnel nexthop",
        "Misc nexthop",
        "Display detail information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 type = CLI_NH_TYPE_MAX;
    bool detail = FALSE;

    index = CTC_CLI_GET_ARGC_INDEX("mcast");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_MCAST;
    }

    index = CTC_CLI_GET_ARGC_INDEX("brguc");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_BRGUC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ipuc");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_IPUC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("mpls");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_MPLS;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ecmp");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_ECMP;
    }

    index = CTC_CLI_GET_ARGC_INDEX("iloop");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_ILOOP;
    }

    index = CTC_CLI_GET_ARGC_INDEX("rspan");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_RSPAN;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-tunnel");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_IP_TUNNEL;
    }

    index = CTC_CLI_GET_ARGC_INDEX("misc");
    if (0xFF != index)
    {
        type = CLI_NH_TYPE_MISC;
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (0xFF != index)
    {
        detail = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_nh_dump_all(lchip, type, detail);
    if (ret)
    {
        ctc_cli_out("%% Dump nexthop fail\n");
    }

    return ret;
}



extern int32
sys_greatbelt_nh_set_ipmc_logic_replication(uint8 lchip, uint8 enable);


CTC_CLI(ctc_cli_gb_nh_cfg_global_param,
        ctc_cli_gb_nh_cfg_global_param_cmd,
        "nexthop (ipmc-logic-replication) value VALUE  (lchip LCHIP|)",
        CTC_CLI_NH_M_STR,
        "IPMC-logic_replication",
        "Value",
        "The Value of paramer configuration",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 value;
    uint8 lchip = 0;
    uint8 index = 0;

    if (0 == sal_memcmp(argv[0], "ipmc-logic-replication", 4))
    {
        CTC_CLI_GET_UINT8("Value", value, argv[1]);

        index = CTC_CLI_GET_ARGC_INDEX("lchip");
        if (0xFF != index)
        {
            CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
        }

        lchip = g_ctcs_api_en?g_api_lchip:lchip;

        ret = sys_greatbelt_nh_set_ipmc_logic_replication(lchip, value);
        if (ret < 0)
        {
            ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_nh_dump_mpls_tunnel,
        ctc_cli_gb_nh_dump_mpls_tunnel_cmd,
        "show nexthop mpls-tunnel TUNNEL_ID (detail|)  (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_NH_M_STR,
        CTC_CLI_NH_MPLS_TUNNEL,
        CTC_CLI_NH_MPLS_TUNNEL_ID,
        "Display detail information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint16 tunnel_id = 0;
    bool detail = FALSE;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT16("Value", tunnel_id, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (0xFF != index)
    {
        detail = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_dump_mpls_tunnel(lchip, tunnel_id, detail);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_nh_set_ecmp_mode,
        ctc_cli_gb_nh_set_ecmp_mode_cmd,
        "nexthop ecmp-mode (enable|disable)  (lchip LCHIP|)",
        CTC_CLI_NH_M_STR,
        "Ecmp mode",
        "Enable means use exact member",
        "Disable means use max member count",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 enable = FALSE;
    uint8 lchip = 0;
    uint8 index = 0;

    if (0 == sal_strncmp("enable", argv[0], sizeof("enable")))
    {
        enable = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_set_ecmp_mode(lchip, enable);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

extern int32
sys_greatbelt_nh_set_reflective_brg_en(uint8 lchip, uint8 enable);

CTC_CLI(ctc_cli_gb_nh_set_reflective_bridge,
        ctc_cli_gb_nh_set_reflective_bridge_cmd,
        "nexthop mcast-reflective-bridge (enable|disable) (lchip LCHIP|)",
        CTC_CLI_NH_M_STR,
        "Mcast packet will support reflective bridge",
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 enable = FALSE;
    uint8 lchip = 0;
    uint8 index = 0;

    if (0 == sal_strncmp("enable", argv[0], sizeof("enable")))
    {
        enable = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_set_reflective_brg_en(lchip, enable);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

extern int32
sys_greatbelt_nh_dump_resource_usage(uint8 lchip);

CTC_CLI(ctc_cli_gb_nh_show_resource_usage,
        ctc_cli_gb_nh_show_resource_usage_cmd,
        "show nexthop status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_NH_M_STR,
        "Nexthop's resource used status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0 ;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_dump_resource_usage(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

extern int32
sys_greatbelt_nh_dump_arp(uint8 lchip, uint16 arp_id, bool detail);
CTC_CLI(ctc_cli_gb_nh_dump_arp_id,
        ctc_cli_gb_nh_dump_arp_id_cmd,
        "show nexthop arp-id ARP_ID (detail|)  (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_NH_M_STR,
        "ARP ID",
        "ARP ID Value",
        "Display detail information",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint16 arp_id = 0;
    uint8  index = 0 ;
    bool detail = FALSE;
    uint8 lchip = 0;

    CTC_CLI_GET_UINT16("Value", arp_id, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX_ENHANCE("detail");
    if (0xFF != index)
    {
         detail = TRUE;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_dump_arp(lchip, arp_id, detail);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

extern int32
sys_greatbelt_nh_set_service_queue_en(uint8 lchip, uint32 nhid, uint8 service_queue_en);

CTC_CLI(ctc_cli_gb_nh_set_service_queue,
        ctc_cli_gb_nh_set_service_queue_cmd,
        "nexthop service-queue NHID (enable|disable)  (lchip LCHIP|)",
        CTC_CLI_NH_M_STR,
        "Service queue",
        CTC_CLI_NH_ID_STR,
        "Enable",
        "Disable",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 service_queue_en = FALSE;
    uint32 nhid = 0;
    int32 ret = CLI_ERROR;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT32("NexthopID", nhid, argv[0]);

    if (0 == sal_strncmp("enable", argv[1], sizeof("enable")))
    {
        service_queue_en = TRUE;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_set_service_queue_en(lchip, nhid, service_queue_en);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

extern int32
sys_greatbelt_nh_update_mcast_service_queue(uint8 lchip, uint16 mc_grp_id, uint32 member_nhid);

CTC_CLI(ctc_cli_gb_nh_update_mcast_service_queue,
        ctc_cli_gb_nh_update_mcast_service_queue_cmd,
        "nexthop mcast-group GROUP_ID service-queue update nexthop NHID  (lchip LCHIP|)",
        CTC_CLI_NH_M_STR,
        "Multicast Group",
        CTC_CLI_GLOBAL_MCASTGRP_ID_DESC,
        "Service queue",
        "Update service queue",
        "Member nexthop",
        CTC_CLI_NH_ID_STR,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    uint16 mc_grp_id = 0;
    uint32 member_nhid = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT16("Mcast group id", mc_grp_id, argv[0]);
    CTC_CLI_GET_UINT32("NexthopID", member_nhid, argv[1]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_nh_update_mcast_service_queue(lchip, mc_grp_id, member_nhid);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n",  ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_nh_merge_dsfwd_mode,
        ctc_cli_gb_nh_merge_dsfwd_mode_cmd,
        "nexthop merge-dsfwd-mode (enable|disable) (lchip LCHIP|)",
        CTC_CLI_NH_M_STR,
        "set nexthop merge dsfwd mode",
        CTC_CLI_ENABLE,
        CTC_CLI_DISABLE,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 have_dsfwd = 0;
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    if (CLI_CLI_STR_EQUAL("en", 0))
    {
        have_dsfwd = 0;
    }
    else
    {
        have_dsfwd = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_nh_set_dsfwd_mode(lchip, have_dsfwd);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_nh_set_nh_drop,
        ctc_cli_gb_nh_set_nh_drop_cmd,
        "nexthop NHID drop (protection-path|) (enable | disable)",
        CTC_CLI_NH_M_STR,
        CTC_CLI_NH_ID_STR,
        "Set nexthop drop",
        "Protection Path",
        "Drop Enable",
        "Drop Disable")
{
    int32 ret       = CLI_SUCCESS;
    uint32 nhid     = 0;
    uint16 index    = 0;
    ctc_nh_drop_info_t nh_drop;

    sal_memset(&nh_drop, 0, sizeof(ctc_nh_drop_info_t));


    CTC_CLI_GET_UINT32("Nexthop ID", nhid, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("protection-path");
    if (0xFF != index)
    {
        nh_drop.is_protection_path = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (0xFF != index)
    {
        nh_drop.drop_en = 1;
    }
    else
    {
        nh_drop.drop_en = 0;
    }

    if (g_ctcs_api_en)
    {
        ret = ctcs_nh_set_nh_drop(g_api_lchip, nhid, &nh_drop);
    }
    else
    {
        ret = ctc_nh_set_nh_drop(nhid, &nh_drop);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_greatbelt_nexthop_cli_init(void)
{

    install_element(CTC_SDK_MODE, &ctc_cli_gb_nh_show_nexthop_by_nhid_type_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_nh_show_nexthop_all_by_type_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_nh_dump_mpls_tunnel_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_nh_show_resource_usage_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_nh_dump_arp_id_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_nh_set_nh_drop_cmd);

    /*GB CLIs*/
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_cfg_global_param_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_set_ecmp_mode_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_set_reflective_bridge_cmd);

    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_set_service_queue_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_update_mcast_service_queue_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_merge_dsfwd_mode_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_nexthop_cli_deinit(void)
{

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_nh_show_nexthop_by_nhid_type_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_nh_show_nexthop_all_by_type_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_nh_dump_mpls_tunnel_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_nh_show_resource_usage_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_nh_dump_arp_id_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_nh_set_nh_drop_cmd);

    /*GB CLIs*/
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_cfg_global_param_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_set_ecmp_mode_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_set_reflective_bridge_cmd);

    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_set_service_queue_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_update_mcast_service_queue_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_nh_merge_dsfwd_mode_cmd);

    return CLI_SUCCESS;
}

