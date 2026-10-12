/**
 @file ctc_mpls_cli.c

 @date 2010-03-16

 @version v2.0

 The file apply clis of ipuc module
*/

#include "sal.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_mpls_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_mpls.h"
#include "ctc_cli_common.h"

#define CTC_CLI_MPLS_L2VPN_OLD_OPT \
    "| igmp-snooping-en | vsi-learning-disable\
        | mac-security-vsi-discard "
#define CTC_CLI_MPLS_POLICER_STR \
    "Set policer type after setting id,default type is flow policer"

#define CTC_CLI_MPLS_COMM_OPT \
    "stats STATS_ID |trust-outer-exp |qos-use-outer-info | acl-use-outer-info |"
#define CTC_CLI_MPLS_COMM_STR \
    "Statistics",\
    "1~0xFFFFFFFF",\
    "Use outer label's exp",\
    "Use the QOS information from outer header",\
    "Use the ACL information from outer header"

extern int32
sys_greatbelt_mpls_ilm_show(uint8 lchip, uint8 space_id, uint32 label_id);

extern int32
sys_greatbelt_show_mpls_status(uint8 lchip);

extern int32
sys_greatbelt_mpls_set_label_range_mode(uint8 lchip, bool mode_en);

extern int32
sys_greatbelt_mpls_set_label_range(uint8 lchip, uint8 block,uint32 start_label,uint32 size);

extern int32
sys_greatbelt_mpls_set_decap_mode(uint8 lchip, uint8 mode);


CTC_CLI(ctc_cli_gb_mpls_show_ilm_,
        ctc_cli_gb_mpls_show_ilm__cmd,
        "show mpls ilm (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_MPLS_M_STR,
        "Ilm entries",
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
    ret = sys_greatbelt_mpls_ilm_show(lchip,0xFF, 0xFFFFFFFF);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }


    return ret;
}

CTC_CLI(ctc_cli_gb_mpls_show_mpls_status,
        ctc_cli_gb_mpls_show_mpls_status_cmd,
        "show mpls status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_MPLS_M_STR,
        "MPLS Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_show_mpls_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;

}


CTC_CLI(ctc_cli_mpls_set_label_range_mode,
        ctc_cli_mpls_set_label_range_mode_cmd,
        "mpls label-range-mode (enable|disable) (lchip LCHIP|)",
        CTC_CLI_MPLS_M_STR,
        "set label range mode",
        CTC_CLI_ENABLE,
        CTC_CLI_DISABLE,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    bool mode_en = FALSE;
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    if (CLI_CLI_STR_EQUAL("en", 0))
    {
        mode_en = TRUE;
    }

    ret = sys_greatbelt_mpls_set_label_range_mode(lchip,mode_en);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_mpls_set_label_range,
        ctc_cli_mpls_set_label_range_cmd,
        "mpls set-block BLOCK_ID start-label LABEL size SIZE (lchip LCHIP|)",
        CTC_CLI_MPLS_M_STR,
        "set label block",
        "block :0-1-2",
        "start label: unit 1K(1024)",
        "label : unit 1K(1024)",
        "label size : unit 1K(1024)",
        "size : unit 1K(1024)",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 block = 0;
    uint32 start_label = 0;
    uint32 size = 0;
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT32("nexthop-id", block, argv[0]);
    CTC_CLI_GET_UINT32("start_label", start_label, argv[1]);
    CTC_CLI_GET_UINT32("label size", size, argv[2]);
    start_label *=1024;
    size *= 1024;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_mpls_set_label_range(lchip,block,start_label,size);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_mpls_set_decap_mode,
        ctc_cli_mpls_set_decap_mode_cmd,
        "mpls decap-mode MODE (lchip LCHIP|)",
        CTC_CLI_MPLS_M_STR,
        "Decap mode",
        "0:normal mode, 1:iloop mode",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 decap_mode = 0;
    uint8 lchip = 0;
    uint8 index = 0;

    CTC_CLI_GET_UINT8("decap-mode", decap_mode, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_mpls_set_decap_mode(lchip,decap_mode);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


CTC_CLI(ctc_cli_gb_mpls_add_pw,
        ctc_cli_gb_mpls_add_pw_cmd,
        "mpls l2vpn-pw add (space SPACEID |) LABEL (vpws NHID \
        | vpls FID {vpls-port-type "CTC_CLI_MPLS_L2VPN_OLD_OPT"| pwid PWID |})\
        { service-aclqos-en | encapsulation-mode (raw|tagged) | control-word \
        | oam | svlan-tpid-index TPID_INDEX \
        | aps-select GROUP_ID (working-path|protection-path) \
        | service-id ID | service-policer-en| service-queue-en \
        | inner-pkt-raw | policer-id POLICERID (service-policer|)\
        | user-vlanptr VLAN_PTR | "CTC_CLI_MPLS_COMM_OPT" }",
        CTC_CLI_MPLS_M_STR,
        "L2vpn pw mapping",
        "Add vpls/vpws pw mapping",
        "Mpls label space",
        "Label space id, <0-255>, space 0 is platform space",
        "L2 vpn vc incoming label, <0-1048575>",
        "Virtual private wire services ",
        CTC_CLI_NH_ID_STR,
        "Virtual private lan services",
        CTC_CLI_FID_ID_DESC,
        "Vpls port type, means this vpls PW is not a H-VPLS tunnel",
        "enable igmp snooping ",
        "disable vsi learning",
        "vsi mac security discard",
        "Logic port",
        "0xffff means no used",
        "Enable service aclqos",
        "PW encapsulation mode",
        "Raw mode",
        "Tagged mode",
        "Control word used",
        "OAM Enable",
        "Set svlan tpid index",
        "Index <0-3>, the corresponding svlan tpid is in EpeL2TpidCtl",
        "APS select ",
        CTC_CLI_APS_GROUP_ID_DESC,
        "Indicate working path",
        "Indicate protection path",
        "Service id",
        "Service policer",
        "Service queue enable",
        "Inner packet raw",
        "Set Policer ID",
        "Policer ID 1 - 16383",
        "service policer",
        "User vlanptr",
        CTC_CLI_USER_VLAN_PTR,
        CTC_CLI_MPLS_POLICER_STR,
        CTC_CLI_MPLS_COMM_STR)
{
    int32 ret = CLI_SUCCESS;
    ctc_mpls_l2vpn_pw_t pw_info;
    uint32 fid;
    int idx = 0;

    sal_memset(&pw_info, 0, sizeof(pw_info));

    idx = CTC_CLI_GET_ARGC_INDEX("space");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT8_RANGE("space", pw_info.space_id, argv[idx + 1], 0, CTC_MAX_UINT8_VALUE);
        CTC_CLI_GET_UINT32("label", pw_info.label, argv[idx + 2]);
    }
    else
    {
        pw_info.space_id = 0;
        CTC_CLI_GET_UINT32("label", pw_info.label, argv[0]);
    }

    idx = CTC_CLI_GET_ARGC_INDEX("vpws");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT32("nexthop", pw_info.u.pw_nh_id, argv[idx + 1]);
        pw_info.l2vpntype = CTC_MPLS_L2VPN_VPWS;
    }

    idx = CTC_CLI_GET_ARGC_INDEX("vpls");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT16_RANGE("fid", fid, argv[idx + 1], 0, CTC_MAX_UINT16_VALUE);
        pw_info.u.vpls_info.fid = fid;
        pw_info.l2vpntype = CTC_MPLS_L2VPN_VPLS;

        idx  = CTC_CLI_GET_ARGC_INDEX("vpls-port-type");
        if (0xFF != idx)
        {
            pw_info.u.vpls_info.logic_port_type = TRUE;
        }

        idx  = CTC_CLI_GET_ARGC_INDEX("igmp-snooping-en");
        if (0xFF != idx)
        {
            pw_info.igmp_snooping_enable = TRUE;
        }

        idx  = CTC_CLI_GET_ARGC_INDEX("vsi-learning-disable");
        if (0xFF != idx)
        {
            pw_info.learn_disable = TRUE;
        }

        idx  = CTC_CLI_GET_ARGC_INDEX("mac-security-vsi-discard");
        if (0xFF != idx)
        {
            pw_info.maclimit_enable = TRUE;
        }
    }

    idx  = CTC_CLI_GET_ARGC_INDEX("pwid");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT16("pwid", pw_info.logic_port, argv[idx + 1]);
    }

    /* qos use outer info */
    idx  = CTC_CLI_GET_ARGC_INDEX("qos-use-outer-info");
    if (0xFF != idx)
    {
        pw_info.qos_use_outer_info = TRUE;
    }

    /* service aclqos en */
    idx  = CTC_CLI_GET_ARGC_INDEX("service-aclqos-en");
    if (0xFF != idx)
    {
        pw_info.service_aclqos_enable = TRUE;
    }

    /* encapsulation mode is RAW for default */
    pw_info.pw_mode = CTC_MPLS_L2VPN_RAW;
    idx  = CTC_CLI_GET_ARGC_INDEX("encapsulation-mode");
    if (0xFF != idx)
    {
        idx  = CTC_CLI_GET_ARGC_INDEX("raw");
        if (0xFF != idx)
        {
            pw_info.pw_mode = CTC_MPLS_L2VPN_RAW;
        }
        else
        {
            pw_info.pw_mode = CTC_MPLS_L2VPN_TAGGED;
        }
    }

    idx  = CTC_CLI_GET_ARGC_INDEX("control-word");
    if (0xFF != idx)
    {
        pw_info.cwen = TRUE;
    }

    idx  = CTC_CLI_GET_ARGC_INDEX("oam");
    if (0xFF != idx)
    {
        pw_info.oam_en = TRUE;
    }

    idx = CTC_CLI_GET_ARGC_INDEX("svlan-tpid-index");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT8_RANGE("svlan-tpid-index", pw_info.svlan_tpid_index, argv[idx + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    idx  = CTC_CLI_GET_ARGC_INDEX("service-id");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT16_RANGE("service-id ", pw_info.service_id, argv[idx + 1], 0, CTC_MAX_UINT16_VALUE);
        pw_info.id_type |= CTC_MPLS_ID_SERVICE;
    }


    /* service policer en */
    idx  = CTC_CLI_GET_ARGC_INDEX("service-policer-en");
    if (0xFF != idx)
    {
        pw_info.service_policer_en = TRUE;
    }

    /* service queue en */
    idx  = CTC_CLI_GET_ARGC_INDEX("service-queue-en");
    if (0xFF != idx)
    {
        pw_info.service_queue_en= TRUE;
    }


    idx = CTC_CLI_GET_ARGC_INDEX("aps-select");
    if (0xFF != idx)
    {
        pw_info.id_type |= CTC_MPLS_ID_APS_SELECT;
        CTC_CLI_GET_UINT16_RANGE("aps-select ", pw_info.aps_select_grp_id, argv[idx + 1], 0, CTC_MAX_UINT16_VALUE);

        if (CLI_CLI_STR_EQUAL("protection-path", idx + 2))
        {
            pw_info.aps_select_protect_path = 1;
        }
        else
        {
            pw_info.aps_select_protect_path = 0;
        }
    }

    idx = CTC_CLI_GET_ARGC_INDEX("stats");
    if (idx != 0xFF)
    {
        pw_info.id_type |= CTC_MPLS_ID_STATS;
        CTC_CLI_GET_UINT32("stats-id", pw_info.stats_id, argv[idx + 1]);
    }

    idx = CTC_CLI_GET_ARGC_INDEX("inner-pkt-raw");
    if (idx != 0xFF)
    {
        pw_info.inner_pkt_type = CTC_MPLS_INNER_RAW;
    }

    idx = CTC_CLI_GET_ARGC_INDEX("igmp-snooping-en");
    if (idx != 0xFF)
    {
        pw_info.igmp_snooping_enable = 1;
    }

    idx = CTC_CLI_GET_ARGC_INDEX("trust-outer-exp");
    if (0xFF != idx )
    {
        pw_info.trust_outer_exp = 1;
    }

    idx = CTC_CLI_GET_ARGC_INDEX("user-vlanptr");
    if (idx != 0xFF)
    {
        pw_info.flag |= CTC_MPLS_ILM_FLAG_L2VPN_OAM;
        CTC_CLI_GET_UINT16("user-vlanptr", pw_info.l2vpn_oam_id, argv[idx + 1]);
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_mpls_add_l2vpn_pw(g_api_lchip, &pw_info);
    }
    else
    {
        ret = ctc_mpls_add_l2vpn_pw(&pw_info);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_mpls_remove_pw,
        ctc_cli_gb_mpls_remove_pw_cmd,
        "mpls l2vpn-pw remove (space SPACEID |) LABEL",
        CTC_CLI_MPLS_M_STR,
        "L2vpn pw mapping",
        "Remove l2vpn pw mapping",
        "Mpls label space",
        "Label space id, <0-255>, space 0 is platform space",
        "L2vpn vc incoming label, <0-1048575>")
{
    int32 ret = CLI_SUCCESS;
    uint8 idx = 0;
    ctc_mpls_l2vpn_pw_t pw_info;

    sal_memset(&pw_info, 0, sizeof(ctc_mpls_l2vpn_pw_t));
    idx = CTC_CLI_GET_ARGC_INDEX("space");
    if (0xFF != idx)
    {
        CTC_CLI_GET_UINT8_RANGE("space", pw_info.space_id, argv[idx + 1], 0, CTC_MAX_UINT8_VALUE);
        CTC_CLI_GET_UINT32("label", pw_info.label, argv[idx + 2]);
    }
    else
    {
        pw_info.space_id = 0;
        CTC_CLI_GET_UINT32("label", pw_info.label, argv[0]);
    }

    if (g_ctcs_api_en)
    {
        ret = ctcs_mpls_del_l2vpn_pw(g_api_lchip, &pw_info);
    }
    else
    {
        ret = ctc_mpls_del_l2vpn_pw(&pw_info);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_mpls_add_vpws_ilm,
        ctc_cli_gb_mpls_add_vpws_ilm_cmd,
        "mpls ilm add vpws-vc SPACEID LABEL NHID (control-word | ) (oam |) (inner-pkt-raw |) (trust-outer-exp |) (SERVICE_ID | stats STATS_ID |)",
        CTC_CLI_MPLS_M_STR,
        "Incoming label mapping",
        "Add ilm",
        "Vpws vc label",
        "Label space id, <0-255>, space 0 is platform space",
        "Label, <0-1048575>",
        CTC_CLI_NH_ID_STR,
        "Control word used",
        "OAM Enable",
        "Don't check inner packet for vpws",
        "Use outer label's exp",
        "Service id",
        "Statistics",
        "0~0xFFFFFFFF")
{
    int32 ret = CLI_SUCCESS;
    uint32 spaceid = 0;
    uint32 sid = 0;
    ctc_mpls_ilm_t ilm_info;
    int idx = 0;
     /* uint8 index = 0;*/

    sal_memset(&ilm_info, 0, sizeof(ctc_mpls_ilm_t));

    CTC_CLI_GET_UINT8_RANGE("space", spaceid, argv[0], 0, CTC_MAX_UINT8_VALUE);
    ilm_info.spaceid = spaceid;

    CTC_CLI_GET_UINT32_RANGE("label", ilm_info.label, argv[1], 0, CTC_MAX_UINT32_VALUE);
    CTC_CLI_GET_UINT32("nexthop", ilm_info.nh_id, argv[2]);
    idx = 3;

    if ((CTC_CLI_GET_ARGC_INDEX("stats")) == 0xFF)
    {
        while (idx < argc)
        {
            if (0 == sal_memcmp("co", argv[idx], 2))
            {
                ilm_info.cwen = TRUE;
                idx++;
                continue;
            }

            if (0 == sal_memcmp("oa", argv[idx], 2))
            {
                ilm_info.oam_en = TRUE;
                idx++;
                continue;
            }

            if (0 == sal_memcmp("inner", argv[idx], 5))
            {
                ilm_info.inner_pkt_type = CTC_MPLS_INNER_RAW;
                idx++;
                continue;
            }

            if (0 == sal_memcmp("trust", argv[idx], 5))
            {
                ilm_info.trust_outer_exp = 1;
                idx++;
                continue;
            }

            CTC_CLI_GET_UINT16_RANGE("service id", sid, argv[idx], 0, CTC_MAX_UINT16_VALUE);
            ilm_info.flw_vrf_srv_aps.service_id = sid;
            ilm_info.id_type |= CTC_MPLS_ID_SERVICE;
            idx++;
        }
    }
    else
    {
        idx = CTC_CLI_GET_ARGC_INDEX("stats");
        ilm_info.id_type |= CTC_MPLS_ID_STATS;
        CTC_CLI_GET_UINT32("stats-id", ilm_info.stats_id, argv[idx + 1]);
    }

#if 0
    index = CTC_CLI_GET_ARGC_INDEX("inner-pkt-reserved");
    if (0xFF != index)
    {
        ilm_info.inner_pkt_type = CTC_MPLS_RESERVED;
    }
#endif

    ilm_info.type = CTC_MPLS_LABEL_TYPE_VPWS;

    if (g_ctcs_api_en)
    {
        ret = ctcs_mpls_add_ilm(g_api_lchip, &ilm_info);
    }
    else
    {
        ret = ctc_mpls_add_ilm(&ilm_info);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_mpls_add_vpls_ilm,
        ctc_cli_gb_mpls_add_vpls_ilm_cmd,
        "mpls ilm add vpls-vc LABEL NHID PWID (vpls-port-type | ) (control-word | ) (oam |) (trust-outer-exp |) (SERVICE_ID | stats STATS_ID |)",
        CTC_CLI_MPLS_M_STR,
        "Incoming label mapping",
        "Add ilm",
        "Vpls vc label",
        "Label, <0-1048575>",
        CTC_CLI_NH_ID_STR,
        "PW ID",
        "Vpls port type, means this vpls PW is not a H-VPLS tunnel",
        "Control word used",
        "OAM Enable",
        "Use outer label's exp",
        "Service id",
        "Statistics",
        "0~0xFFFFFFFF")
{
    int32 ret = CLI_SUCCESS;
    uint32 pwid = 0;
    uint32 sid = 0;
    ctc_mpls_ilm_t ilm_info;
    int idx = 0;

    sal_memset(&ilm_info, 0, sizeof(ctc_mpls_ilm_t));

    CTC_CLI_GET_UINT32("label", ilm_info.label, argv[0]);
    CTC_CLI_GET_UINT32("nexthop", ilm_info.nh_id, argv[1]);
    CTC_CLI_GET_UINT16_RANGE("pwid", pwid, argv[2], 0, CTC_MAX_UINT16_VALUE);
    ilm_info.pwid = pwid;

    idx = 3;

    if ((CTC_CLI_GET_ARGC_INDEX("stats")) == 0xFF)
    {
        while (idx < argc)
        {
            if (0 == sal_memcmp("vp", argv[idx], 2))
            {
                ilm_info.logic_port_type = TRUE;
                idx++;
                continue;
            }

            if (0 == sal_memcmp("co", argv[idx], 2))
            {
                ilm_info.cwen = TRUE;
                idx++;
                continue;
            }

            if (0 == sal_memcmp("oa", argv[idx], 2))
            {
                ilm_info.oam_en = TRUE;
                idx++;
                continue;
            }

            if (0 == sal_memcmp("trust", argv[idx], 5))
            {
                ilm_info.trust_outer_exp = 1;
                idx++;
                continue;
            }

            CTC_CLI_GET_UINT16_RANGE("service id", sid, argv[idx], 0, CTC_MAX_UINT16_VALUE);
            ilm_info.flw_vrf_srv_aps.service_id = sid;
            ilm_info.id_type |= CTC_MPLS_ID_SERVICE;
            idx++;
        }
    }
    else
    {
        idx = CTC_CLI_GET_ARGC_INDEX("stats");
        ilm_info.id_type |= CTC_MPLS_ID_STATS;
        CTC_CLI_GET_UINT32("stats-id", ilm_info.stats_id, argv[idx + 1]);
    }

    ilm_info.spaceid = 0;
    ilm_info.type = CTC_MPLS_LABEL_TYPE_VPLS;

    if (g_ctcs_api_en)
    {
        ret = ctcs_mpls_add_ilm(g_api_lchip, &ilm_info);
    }
    else
    {
        ret = ctc_mpls_add_ilm(&ilm_info);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


int32
ctc_greatbelt_mpls_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_show_ilm__cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_show_mpls_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_add_pw_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_remove_pw_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_add_vpws_ilm_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_add_vpls_ilm_cmd);
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_mpls_set_label_range_cmd);
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_mpls_set_label_range_mode_cmd);
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_mpls_set_decap_mode_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_mpls_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_show_ilm__cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_show_mpls_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_add_pw_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_remove_pw_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_add_vpws_ilm_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_mpls_add_vpls_ilm_cmd);
    uninstall_element(CTC_INTERNAL_MODE,  &ctc_cli_mpls_set_label_range_cmd);
    uninstall_element(CTC_INTERNAL_MODE,  &ctc_cli_mpls_set_label_range_mode_cmd);
    uninstall_element(CTC_INTERNAL_MODE,  &ctc_cli_mpls_set_decap_mode_cmd);

    return CLI_SUCCESS;
}

