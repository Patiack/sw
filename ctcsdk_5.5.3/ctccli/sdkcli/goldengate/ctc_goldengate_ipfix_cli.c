/**
 @file ctc_ipfix_cli.c

 @date 2010-06-09

 @version v2.0

 This file defines functions for ptp cli module

*/

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"
#include "ctc_ipfix_cli.h"
#include "ctc_ipfix.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"

extern int32
sys_goldengate_ipfix_get_entry_by_key(uint8 lchip, ctc_ipfix_data_t* p_key, uint32* p_rst_hit, uint32* p_rst_key_index);
extern int32
sys_goldengate_ipfix_delete_ad_by_index(uint8 lchip, uint32 index);
extern int32
sys_goldengate_ipfix_delete_entry_by_index(uint8 lchip, uint8 type, uint32 index);
extern int32
sys_goldengate_ipfix_get_ad_by_index(uint8 lchip, uint32 index, ctc_ipfix_data_t* p_out);
extern int32
sys_goldengate_ipfix_get_entry_by_index(uint8 lchip, uint32 index, uint8 key_type, ctc_ipfix_data_t* p_out);
extern int32
sys_goldengate_ipfix_show_status(uint8 lchip, void *user_data);
extern int32
sys_goldengate_ipfix_show_stats(uint8 lchip, void *user_data);
extern int32
sys_goldengate_ipfix_clear_stats(uint8 lchip, void * user_data);
extern int32
sys_goldengate_ipfix_show_entry_info(ctc_ipfix_data_t* p_info, void* user_data);
extern int32
_ctc_cli_ipfix_parser_mpls_key(unsigned char argc,char** argv, ctc_ipfix_data_t *data);

CTC_CLI(ctc_cli_gg_ipfix_set_port_cfg,
        ctc_cli_gg_ipfix_set_port_cfg_cmd,
        "ipfix port GPHYPORT_ID (ingress|egress) {lkup-type TYPE|field-sel-id ID|sampling-interval INTERVAL|tcpend-detect-dis VALUE | flow-type TYPE|learn-disable VALUE}",
         CTC_CLI_IPFIX_M_STR,
         CTC_CLI_PORT_M_STR,
         CTC_CLI_GPHYPORT_ID_DESC,
          "Ingress",
          "Egress",
          "Lookup packet type",
          "0-Disable;1-Only L2 Field;2-Only L3 Field; 3 - L2+ L3 Field",
          CTC_CLI_FIELD_SEL_ID_DESC,
          CTC_CLI_FIELD_SEL_ID_VALUE,
          "Sampling interval(packet cnt)",
          CTC_CLI_UINT32_VAR_STR,
          "TCP End Detect",
          CTC_CLI_BOOL_VAR_STR,
          "IPFix aim to flow packet Type",
          "0-all packet;1-non-discard packet;2-discard packet",
          "Learn disable",
          "Disable learn new flow based port")
{
    int32 ret = 0;
    uint16 gport = 0;
    uint8 index = 0;
    ctc_ipfix_port_cfg_t port_cfg;

    sal_memset(&port_cfg, 0, sizeof(port_cfg));

    CTC_CLI_GET_UINT16("gport", gport, argv[0]);

    index = CTC_CLI_GET_ARGC_INDEX("ingress");
    if(index != 0xFF)
    {
        port_cfg.dir = CTC_INGRESS;
    }
    else
    {
        port_cfg.dir = CTC_EGRESS;
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_ipfix_get_port_cfg(g_api_lchip, gport,&port_cfg);
    }
    else
    {
        ret = ctc_ipfix_get_port_cfg(gport,&port_cfg);
    }

    index = CTC_CLI_GET_ARGC_INDEX("lkup-type");
    if(index != 0xFF)
    {
        CTC_CLI_GET_UINT8("lkup-type", port_cfg.lkup_type, argv[index+1]);
    }
    index = CTC_CLI_GET_ARGC_INDEX("field-sel-id");
    if(index != 0xFF)
    {
        CTC_CLI_GET_UINT8("field-sel-id", port_cfg.field_sel_id, argv[index+1]);
    }
    index = CTC_CLI_GET_ARGC_INDEX("tcpend-detect-dis");
    if(index != 0xFF)
    {
        CTC_CLI_GET_UINT8("tcpend-detect", port_cfg.tcp_end_detect_disable, argv[index+1]);
    }
    index = CTC_CLI_GET_ARGC_INDEX("learn-disable");
    if(index != 0xFF)
    {
        CTC_CLI_GET_UINT8("learn-disable", port_cfg.learn_disable, argv[index+1]);
    }
    index = CTC_CLI_GET_ARGC_INDEX("sampling-interval");
    if(index != 0xFF)
    {
        CTC_CLI_GET_UINT16("sampling-interval", port_cfg.sample_interval, argv[index+1]);
    }
    index = CTC_CLI_GET_ARGC_INDEX("flow-type");
    if(index != 0xFF)
    {
        CTC_CLI_GET_UINT8("flow-type", port_cfg.flow_type, argv[index+1]);
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_ipfix_set_port_cfg(g_api_lchip, gport,&port_cfg);
    }
    else
    {
        ret = ctc_ipfix_set_port_cfg(gport,&port_cfg);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}
CTC_CLI(ctc_cli_gg_ipfix_show_port_cfg,
        ctc_cli_gg_ipfix_show_port_cfg_cmd,
        "show ipfix port-cfg GPHYPORT_ID",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPFIX_M_STR,
        "port config for ipfix",
        CTC_CLI_GPHYPORT_ID_DESC)
{
    int32 ret = 0;
    uint16 gport = 0;
    uint8 cnt = 0;
    ctc_ipfix_port_cfg_t ipfix_cfg;

    sal_memset(&ipfix_cfg, 0, sizeof(ctc_ipfix_port_cfg_t));

    CTC_CLI_GET_UINT16("gport", gport, argv[0]);

    ctc_cli_out("\n");
    for(cnt = 0; cnt < 2; cnt++)
    {
        ipfix_cfg.dir = cnt;

        if(g_ctcs_api_en)
        {
             ret = ctcs_ipfix_get_port_cfg(g_api_lchip, gport, &ipfix_cfg);
        }
        else
        {
            ret = ctc_ipfix_get_port_cfg(gport, &ipfix_cfg);
        }
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
            return ret;
        }

        ctc_cli_out("%s\n", (ipfix_cfg.dir)?"Egress Direction Config":"Ingress Direction Config" );
        ctc_cli_out("%-25s: %d\n", "Field Select Id", ipfix_cfg.field_sel_id);
        ctc_cli_out("%-25s: %s\n", "Lookup Type", (ipfix_cfg.lkup_type==0)?"Disable":
            ((ipfix_cfg.lkup_type==1)?"L2":((ipfix_cfg.lkup_type==2)?"L3":"L2L3")));
        ctc_cli_out("%-25s: %d\n", "Sample Pkt Interval", ipfix_cfg.sample_interval);
        ctc_cli_out("%-25s: %s\n", "Tcp End Detect", ipfix_cfg.tcp_end_detect_disable?"Disable":"Enable");
        ctc_cli_out("%-25s: %s\n", "Learn Enable", ipfix_cfg.learn_disable?"Disable":"Enable");
        ctc_cli_out("%-25s: %s\n\n", "Flow Type", (ipfix_cfg.flow_type==0)?"All packet":((ipfix_cfg.flow_type==1)?"No Discard packet":"Discard packet"));
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gg_ipfix_delete_by_index,
        ctc_cli_gg_ipfix_delete_by_index_cmd,
        "ipfix delete (mac-key|ipv4-key|ipv6-key|mpls-key|l2l3-key)  by index IDX (lchip LCHIP|)",
        CTC_CLI_IPFIX_M_STR,
        "Delete",
        "L2 key",
        "Ipv4 key",
        "Ipv6 key",
        "Mpls key",
        "L2l4 key",
        "By",
        "Index",
        "Index value",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint32 index = 0;
    uint8 type = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;

    if (0 == sal_memcmp("mac", argv[0], sal_strlen("mac")))
    {
        type = CTC_IPFIX_KEY_HASH_MAC;
    }
    else if (0 == sal_memcmp("ipv4", argv[0], sal_strlen("ipv4")))
    {
        type = CTC_IPFIX_KEY_HASH_IPV4;
    }
    else if (0 == sal_memcmp("ipv6", argv[0], sal_strlen("ipv6")))
    {
        type = CTC_IPFIX_KEY_HASH_IPV6;
    }
    else if (0 == sal_memcmp("mpls", argv[0], sal_strlen("mpls")))
    {
        type = CTC_IPFIX_KEY_HASH_MPLS;
    }
    else if (0 == sal_memcmp("l2l3", argv[0], sal_strlen("l2l3")))
    {
        type = CTC_IPFIX_KEY_HASH_L2_L3;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    CTC_CLI_GET_UINT32_RANGE("index", index, argv[1],0, CTC_MAX_UINT32_VALUE);


    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_delete_ad_by_index(lchip, index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ret = sys_goldengate_ipfix_delete_entry_by_index(lchip, type, index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_status,
        ctc_cli_gg_ipfix_show_status_cmd,
        "show ipfix status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPFIX_M_STR,
        "Ipfix Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_show_status(lchip, NULL);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_stats,
        ctc_cli_gg_ipfix_show_stats_cmd,
        "show ipfix stats (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPFIX_M_STR,
        "Ipfix Stats",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_show_stats(lchip, NULL);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_clear_stats,
        ctc_cli_gg_ipfix_clear_stats_cmd,
        "clear stats ipfix (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPFIX_M_STR,
        "Clear Ipfix Stats",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_clear_stats(lchip, NULL);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gg_ipfix_show_l2_hashkey,
        ctc_cli_gg_ipfix_show_l2_hashkey_cmd,
        "show ipfix entry-info by entry-key mac-key src-gport SRCPORT" "{"CTC_CLI_IPFIX_KEY_PORT_STR "|" CTC_CLI_IPFIX_KEY_L2_STR "} (dir DIR) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        "By",
        "Entry key",
        "L2 key",
        "Source phyPort",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_KEY_PORT_STR_DESC,
        CTC_CLI_IPFIX_KEY_L2_STR_DESC,
        "Direction",
        "<0:Ingress,1:Egress>",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    ctc_ipfix_data_t use_data;
    uint8 index = 0;
    uint32 rst_hit;
    uint32 rst_key_index;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 detail = 0;
    uint32 entry_idx = 0;

    sal_memset(&use_data, 0, sizeof(ctc_ipfix_data_t));

    use_data.key_type = CTC_IPFIX_KEY_HASH_MAC;

    CTC_CLI_GET_UINT16_RANGE("src-gport", use_data.gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("gport");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("gport", use_data.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("logic-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("lport", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("metadata");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("metadata", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    index = CTC_CLI_GET_ARGC_INDEX("mac-da");
    if (index != 0xFF)
    {
        CTC_CLI_GET_MAC_ADDRESS("mac-da", use_data.dst_mac, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("mac-sa");
    if (index != 0xFF)
    {
        CTC_CLI_GET_MAC_ADDRESS("mac-sa", use_data.src_mac, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("eth-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("ether type", use_data.ether_type, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("svlan");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("svlan", use_data.svlan, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("cvlan");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("cvlan", use_data.cvlan, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("svlan-prio");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("svlan prio", use_data.svlan_prio, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("cvlan-prio");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("cvlan prio", use_data.cvlan_prio, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("svlan-cfi");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("svlan cfi", use_data.svlan_cfi, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("cvlan-cfi");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("cvlan cfi", use_data.cvlan_cfi, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dir");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("dir", use_data.dir, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_get_entry_by_key(lchip, &use_data, &rst_hit, &rst_key_index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");
    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (detail == 1)
    {
        entry_idx = rst_key_index + (detail<<16);
    }
    else
    {
        entry_idx = rst_key_index;
    }

    ret = sys_goldengate_ipfix_show_entry_info(&use_data, &entry_idx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_l2l3_hashkey,
        ctc_cli_gg_ipfix_show_l2l3_hashkey_cmd,
        "show ipfix entry-info by entry-key l2l3-key src-gport SRCPORT" CTC_CLI_IPFIX_L2_L3_KEY_STR "} (dir DIR) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        "By",
        "Entry key",
        "L2L3 key",
        "Source phyPort",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_IPFIX_L2_L3_KEY_STR_DESC,
        "Direction",
        "<0:Ingress,1:Egress>",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    ctc_ipfix_data_t use_data;
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint8 mask_len = 0;
    uint32 temp = 0;
    uint32 rst_hit = 0;
    uint32 rst_key_index = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 detail = 0;
    uint32 entry_idx = 0;

    sal_memset(&use_data, 0, sizeof(ctc_ipfix_data_t));

    use_data.key_type = CTC_IPFIX_KEY_HASH_L2_L3;

    CTC_CLI_GET_UINT16_RANGE("src-gport", use_data.gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("gport");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("gport", use_data.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("logic-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("lport", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("metadata");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("metadata", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    index = CTC_CLI_GET_ARGC_INDEX("mac-da");
    if (index != 0xFF)
    {
        CTC_CLI_GET_MAC_ADDRESS("mac-da", use_data.dst_mac, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("mac-sa");
    if (index != 0xFF)
    {
        CTC_CLI_GET_MAC_ADDRESS("mac-sa", use_data.src_mac, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("eth-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("ether type", use_data.ether_type, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("svlan");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("svlan", use_data.svlan, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("svlan-prio");
    if (index != 0xFF)
    {
        use_data.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
        CTC_CLI_GET_UINT16_RANGE("svlan piro", use_data.svlan_prio, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("svlan-cfi");
    if (index != 0xFF)
    {
        use_data.flags |= CTC_IPFIX_DATA_SVLAN_TAGGED;
        CTC_CLI_GET_UINT16_RANGE("svlan cfi", use_data.svlan_cfi, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("cvlan");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("cvlan", use_data.cvlan, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
    }

    index = CTC_CLI_GET_ARGC_INDEX("cvlan-prio");
    if (index != 0xFF)
    {
        use_data.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
        CTC_CLI_GET_UINT16_RANGE("cvlan prio", use_data.cvlan_prio, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("cvlan-cfi");
    if (index != 0xFF)
    {
        use_data.flags |= CTC_IPFIX_DATA_CVLAN_TAGGED;
        CTC_CLI_GET_UINT16_RANGE("cvlan cfi", use_data.cvlan_cfi, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-sa");
    if (index != 0xFF)
    {
        CTC_CLI_GET_IPV4_ADDRESS("ipsa", temp, argv[index+1]);
        sub_idx = CTC_CLI_GET_ARGC_INDEX("mask-len");
        CTC_CLI_GET_UINT8_RANGE("mask", mask_len, argv[sub_idx+1],0, CTC_MAX_UINT8_VALUE);
        use_data.l3_info.ipv4.ipsa = temp;
        use_data.l3_info.ipv4.ipsa_masklen = mask_len;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-da");
    if (index != 0xFF)
    {
        CTC_CLI_GET_IPV4_ADDRESS("ipda", temp, argv[index+1]);
        sub_idx = CTC_CLI_GET_ARGC_INDEX("mask-len");
        CTC_CLI_GET_UINT8_RANGE("mask", mask_len, argv[sub_idx+1],0, CTC_MAX_UINT8_VALUE);
        use_data.l3_info.ipv4.ipda = temp;
        use_data.l3_info.ipv4.ipda_masklen = mask_len;
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-src-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-src", use_data.l4_info.l4_port.source_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-dst-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-dst", use_data.l4_info.l4_port.dest_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("icmp-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("icmp-type", use_data.l4_info.icmp.icmp_type, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("icmp-code");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("icmp-code", use_data.l4_info.icmp.icmpcode, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("gre-key");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("gre-key", use_data.l4_info.gre_key, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("vni");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("vni", use_data.l4_info.vni, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("ttl");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("ttl", use_data.l3_info.ipv4.ttl, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("vrfid");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("vrfid", use_data.l3_info.vrfid, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("dscp");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("dscp", use_data.l3_info.ipv4.dscp, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("ecn");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("ecn", use_data.l3_info.ipv4.ecn, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-sub-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-sub-type", use_data.l4_info.l4_sub_type, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("aware-tunnel-info-en");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("aware-tunnel-info-en", use_data.l4_info.aware_tunnel_info_en, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("l4-type", use_data.l4_info.type.l4_type, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("dir");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("dir", use_data.dir, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    _ctc_cli_ipfix_parser_mpls_key(argc, &argv[0], &use_data);

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_get_entry_by_key(lchip, &use_data, &rst_hit, &rst_key_index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");
    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (detail == 1)
    {
        entry_idx = rst_key_index + (detail<<16);
    }
    else
    {
        entry_idx = rst_key_index;
    }

    ret = sys_goldengate_ipfix_show_entry_info(&use_data, &entry_idx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_ipv4_hashkey,
        ctc_cli_gg_ipfix_show_ipv4_hashkey_cmd,
        "show ipfix entry-info by entry-key ipv4-key src-gport SRCPORT" "{"CTC_CLI_IPFIX_KEY_PORT_STR "| "CTC_CLI_IPFIX_KEY_L3_STR"|"CTC_CLI_IPFIX_KEY_IPV4_L4_STR"} (dir DIR) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        "By",
        "Entry key",
        "Ipv4 key",
        "Source phyPort",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_KEY_PORT_STR_DESC,
        CTC_CLI_IPFIX_KEY_L3_STR_DESC,
        CTC_CLI_IPFIX_KEY_IPV4_L4_STR_DESC,
        "Direction",
        "<0:Ingress,1:Egress>",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    ctc_ipfix_data_t use_data;
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint8 mask_len = 0;
    uint32 temp = 0;
    uint32 rst_hit = 0;
    uint32 rst_key_index = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 detail = 0;
    uint32 entry_idx = 0;

    sal_memset(&use_data, 0, sizeof(ctc_ipfix_data_t));

    use_data.key_type = CTC_IPFIX_KEY_HASH_IPV4;

    CTC_CLI_GET_UINT16_RANGE("src-gport", use_data.gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("gport");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("gport", use_data.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("logic-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("lport", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("metadata");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("metadata", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-sa");
    if (index != 0xFF)
    {
        CTC_CLI_GET_IPV4_ADDRESS("ipsa", temp, argv[index+1]);
        sub_idx = CTC_CLI_GET_ARGC_INDEX("mask-len");
        CTC_CLI_GET_UINT8_RANGE("mask", mask_len, argv[sub_idx+1],0, CTC_MAX_UINT8_VALUE);
        use_data.l3_info.ipv4.ipsa = temp;
        use_data.l3_info.ipv4.ipsa_masklen = mask_len;

    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-da");
    if (index != 0xFF)
    {
        CTC_CLI_GET_IPV4_ADDRESS("ipda", temp, argv[index+1]);
        sub_idx = CTC_CLI_GET_ARGC_INDEX("mask-len");
        CTC_CLI_GET_UINT8_RANGE("mask", mask_len, argv[sub_idx+1],0, CTC_MAX_UINT8_VALUE);
        use_data.l3_info.ipv4.ipda = temp;
        use_data.l3_info.ipv4.ipda_masklen = mask_len;
    }

    index = CTC_CLI_GET_ARGC_INDEX("dscp");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("dscp", use_data.l3_info.ipv4.dscp, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-protocol");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("ip-protocol", use_data.l4_info.type.ip_protocol, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-sub-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-sub-type", use_data.l4_info.l4_sub_type, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("aware-tunnel-info-en");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("aware-tunnel-info-en", use_data.l4_info.aware_tunnel_info_en, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-src-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-src", use_data.l4_info.l4_port.source_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-dst-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-dst", use_data.l4_info.l4_port.dest_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("icmp-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("icmp-type", use_data.l4_info.icmp.icmp_type, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("icmp-code");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("icmp-code", use_data.l4_info.icmp.icmpcode, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("gre-key");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("gre-key", use_data.l4_info.gre_key, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("vni");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("vni", use_data.l4_info.vni, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("dir");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("dir", use_data.dir, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_get_entry_by_key(lchip, &use_data, &rst_hit, &rst_key_index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");
    if (detail == 1)
    {
        entry_idx = rst_key_index + (detail<<16);
    }
    else
    {
        entry_idx = rst_key_index;
    }

    ret = sys_goldengate_ipfix_show_entry_info(&use_data, &entry_idx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_mpls_hashkey,
        ctc_cli_gg_ipfix_show_mpls_hashkey_cmd,
        "show ipfix entry-info by entry-key mpls-key src-gport SRCPORT" "{"CTC_CLI_IPFIX_KEY_PORT_STR "|" CTC_CLI_IPFIX_KEY_MPLS_STR "}" " (dir DIR) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        "By",
        "Entry key",
        "Mpls key",
        "Source phyPort",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_KEY_PORT_STR_DESC,
        CTC_CLI_IPFIX_KEY_MPLS_STR_DESC,
        "Direction",
        "<0:Ingress,1:Egress>",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    ctc_ipfix_data_t use_data;
    uint8 index = 0;
    uint32 rst_hit = 0;
    uint32 rst_key_index = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 detail = 0;
    uint32 entry_idx = 0;

    sal_memset(&use_data, 0, sizeof(ctc_ipfix_data_t));

    use_data.key_type = CTC_IPFIX_KEY_HASH_MPLS;

    CTC_CLI_GET_UINT16_RANGE("src-gport", use_data.gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("gport");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("gport", use_data.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("logic-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("lport", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("metadata");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("metadata", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    _ctc_cli_ipfix_parser_mpls_key(argc, &argv[0], &use_data);

    index = CTC_CLI_GET_ARGC_INDEX("dir");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("dir", use_data.dir, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_get_entry_by_key(lchip, &use_data, &rst_hit, &rst_key_index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");
    if (detail == 1)
    {
        entry_idx = rst_key_index + (detail<<16);
    }
    else
    {
        entry_idx = rst_key_index;
    }

    ret = sys_goldengate_ipfix_show_entry_info(&use_data, &entry_idx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_ipv6_hashkey,
        ctc_cli_gg_ipfix_show_ipv6_hashkey_cmd,
        "show ipfix entry-info by entry-key ipv6-key src-gport SRCPORT" "{"CTC_CLI_IPFIX_KEY_PORT_STR "| ip-sa X:X::X:X mask-len LEN| ip-da X:X::X:X mask-len LEN|dscp DSCP|flow-label FLOW_LABEL|"CTC_CLI_IPFIX_KEY_L4_STR"} (dir DIR) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        "By",
        "Entry key",
        "Ipv6 key",
        "Source phyPort",
        CTC_CLI_GPORT_ID_DESC,
        CTC_CLI_KEY_PORT_STR_DESC,
        "ip-sa",
        CTC_CLI_IPV6_FORMAT,
        "mask-len",
        "mask len value<8,12,16,...,128>",
        "ip-da",
        CTC_CLI_IPV6_FORMAT,
        "mask-len",
        "mask len value<8,12,16,...,128>",
        "dscp",
        "Dscp value",
        "flow label",
        "Flow label value, it will cover l4_port",
        CTC_CLI_IPFIX_KEY_L4_STR_DESC,
        "Direction",
        "<0:Ingress,1:Egress>",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    ctc_ipfix_data_t use_data;
    uint8 index = 0;
    uint8 sub_idx = 0;
    uint8 mask_len = 0;
    ipv6_addr_t ipv6_address;
    uint32 rst_hit = 0;
    uint32 rst_key_index = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint32 detail = 0;
    uint32 entry_idx = 0;

    sal_memset(&use_data, 0, sizeof(ctc_ipfix_data_t));

    use_data.key_type = CTC_IPFIX_KEY_HASH_IPV6;

    CTC_CLI_GET_UINT16_RANGE("src-gport", use_data.gport, argv[0], 0, CTC_MAX_UINT16_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("gport");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("gport", use_data.gport, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_GPORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("logic-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("lport", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_LOGIC_PORT;
    }

    index = CTC_CLI_GET_ARGC_INDEX("metadata");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("metadata", use_data.logic_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
        use_data.port_type = CTC_IPFIX_PORT_TYPE_METADATA;
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-sa");
    if (index != 0xFF)
    {
        CTC_CLI_GET_IPV6_ADDRESS("ipv6", ipv6_address, argv[index+1]);
        sub_idx = CTC_CLI_GET_ARGC_INDEX("mask-len");
        CTC_CLI_GET_UINT8_RANGE("mask", mask_len, argv[sub_idx+1],0, CTC_MAX_UINT8_VALUE);
        use_data.l3_info.ipv6.ipsa_masklen = mask_len;
        use_data.l3_info.ipv6.ipsa[0] = sal_htonl(ipv6_address[0]);
        use_data.l3_info.ipv6.ipsa[1] = sal_htonl(ipv6_address[1]);
        use_data.l3_info.ipv6.ipsa[2] = sal_htonl(ipv6_address[2]);
        use_data.l3_info.ipv6.ipsa[3] = sal_htonl(ipv6_address[3]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("ip-da");
    if (index != 0xFF)
    {
        CTC_CLI_GET_IPV6_ADDRESS("ipda", ipv6_address, argv[index+1]);
        sub_idx = CTC_CLI_GET_ARGC_INDEX("mask-len");
        CTC_CLI_GET_UINT8_RANGE("mask", mask_len, argv[sub_idx+1],0, CTC_MAX_UINT8_VALUE);
        use_data.l3_info.ipv6.ipda_masklen = mask_len;
        use_data.l3_info.ipv6.ipda[0] = sal_htonl(ipv6_address[0]);
        use_data.l3_info.ipv6.ipda[1] = sal_htonl(ipv6_address[1]);
        use_data.l3_info.ipv6.ipda[2] = sal_htonl(ipv6_address[2]);
        use_data.l3_info.ipv6.ipda[3] = sal_htonl(ipv6_address[3]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("l4-type", use_data.l4_info.type.l4_type, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-sub-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-sub-type", use_data.l4_info.l4_sub_type, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("aware-tunnel-info-en");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("aware-tunnel-info-en", use_data.l4_info.aware_tunnel_info_en, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-src-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-src", use_data.l4_info.l4_port.source_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("l4-dst-port");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT16_RANGE("l4-dst", use_data.l4_info.l4_port.dest_port, argv[index + 1], 0, CTC_MAX_UINT16_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("icmp-type");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("icmp-type", use_data.l4_info.icmp.icmp_type, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("icmp-code");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("icmp-code", use_data.l4_info.icmp.icmpcode, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("gre-key");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("gre-key", use_data.l4_info.gre_key, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("dscp");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT8_RANGE("dscp", use_data.l3_info.ipv6.dscp, argv[index + 1], 0, CTC_MAX_UINT8_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("flow-label");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("flow label", use_data.l3_info.ipv6.flow_label, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("vni");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("vni", use_data.l4_info.vni, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("dir");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("dir", use_data.dir, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_get_entry_by_key(lchip, &use_data, &rst_hit, &rst_key_index);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");
    if (detail == 1)
    {
        entry_idx = rst_key_index + (detail<<16);
    }
    else
    {
        entry_idx = rst_key_index;
    }

    ret = sys_goldengate_ipfix_show_entry_info(&use_data, &entry_idx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }


    ctc_cli_out("\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_show_by_index,
        ctc_cli_gg_ipfix_show_by_index_cmd,
        "show ipfix entry-info by (entry-idx IDX) (entry-type (mac-key|ipv4-key|ipv6-key|mpls-key|l2l3-key)) (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        "By",
        "Entry index",
        "Entry index vlave",
        "Entry type",
        "L2 key",
        "Ipv4 key",
        "Ipv6 key",
        "Mpls key",
        "L2l3 key",
        "Detail",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint32 entry_idx = 0;
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    ctc_ipfix_data_t  info;
    uint32 detail = 0;
    uint8 index = 0;
    uint8 key_type = 0;

    sal_memset(&info, 0, sizeof(ctc_ipfix_data_t));

    index = CTC_CLI_GET_ARGC_INDEX("entry-type");
    if(index != 0xFF)
    {
        if (0 == sal_memcmp("mac", argv[index + 1], sal_strlen("mac")))
        {
            key_type = CTC_IPFIX_KEY_HASH_MAC;
        }
        else if (0 == sal_memcmp("ipv4", argv[index + 1], sal_strlen("ipv4")))
        {
            key_type = CTC_IPFIX_KEY_HASH_IPV4;
        }
        else if (0 == sal_memcmp("ipv6", argv[index + 1], sal_strlen("ipv6")))
        {
            key_type = CTC_IPFIX_KEY_HASH_IPV6;
        }
        else if (0 == sal_memcmp("mpls", argv[index + 1], sal_strlen("mpls")))
        {
            key_type = CTC_IPFIX_KEY_HASH_MPLS;
        }
        else if (0 == sal_memcmp("l2l3", argv[index + 1], sal_strlen("l2l3")))
        {
            key_type = CTC_IPFIX_KEY_HASH_L2_L3;
        }
    }

    index = CTC_CLI_GET_ARGC_INDEX("entry-idx");
    if (index != 0xFF)
    {
        CTC_CLI_GET_UINT32_RANGE("entry idx", entry_idx, argv[index + 1], 0, CTC_MAX_UINT32_VALUE);
    }

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_goldengate_ipfix_get_entry_by_index(lchip, entry_idx, key_type, &info);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ret = sys_goldengate_ipfix_get_ad_by_index(lchip, entry_idx, &info);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");
    if(detail == 1)
    {
        entry_idx = entry_idx + (detail<<16);
    }

    ret = sys_goldengate_ipfix_show_entry_info(&info, &entry_idx);
    if (ret < 0)
    {
        ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("\n");

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ipfix_dump,
        ctc_cli_gg_ipfix_dump_cmd,
        "show ipfix entry-info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Ipfix module",
        "Entry info",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)

{
    int32 ret = CLI_SUCCESS;
    uint16 traverse_action = 0;
    ctc_ipfix_traverse_t ipfix_traverse;
    uint8 lchip = 0;
    uint8 index = 0;

    sal_memset(&ipfix_traverse, 0, sizeof(ctc_ipfix_traverse_t));
     /*traverse_action = SYS_IPFIX_DUMP_ENYRY_INFO;*/
    traverse_action = 0x0011;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    ctc_cli_out("\n%-11s%-12s%-7s%-8s%-9s%-9s%-12s%-7s\n", "ENTRY_IDX", "ENTRY_TYPE", "PORT", "SEL_ID", "MAX_TTL", "MIN_TTL", "TIMESTAMP", "DIR");
    ctc_cli_out("--------------------------------------------------------------------------\n");

    while (!ipfix_traverse.is_end)
    {
        ipfix_traverse.entry_num = 100;
        ipfix_traverse.start_index = ipfix_traverse.next_traverse_index;
        ipfix_traverse.user_data = & traverse_action;

        if (g_ctcs_api_en)
        {
            ret = ctcs_ipfix_traverse(g_api_lchip, sys_goldengate_ipfix_show_entry_info, &ipfix_traverse);
        }
        else
        {
            ret = ctc_ipfix_traverse(sys_goldengate_ipfix_show_entry_info, &ipfix_traverse);
        }
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }

    ctc_cli_out("\n");

    return CLI_SUCCESS;
}
int32
sys_goldengate_ipfix_remove_cmp(ctc_ipfix_data_t* p_info, void* user_data)
{
    return 0;
}
CTC_CLI(ctc_cli_goldengate_ipfix_remove_all_,
         ctc_cli_goldengate_ipfix_remove_all_cmd,
         "ipfix remove-all entry (lchip LCHIP|)",
         CTC_CLI_IPFIX_M_STR,
         "remove all",
         "entry",
         CTC_CLI_LCHIP_ID_STR,
         CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 index = 0;
    uint16 traverse_action = 0;
    uint8 lchip = 0;
    ctc_ipfix_traverse_t ipfix_traverse;

    sal_memset(&ipfix_traverse, 0, sizeof(ctc_ipfix_traverse_t));

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    ipfix_traverse.lchip_id = lchip;

    while (!ipfix_traverse.is_end)
    {
        ipfix_traverse.entry_num = 100;
        ipfix_traverse.start_index = ipfix_traverse.next_traverse_index;
        ipfix_traverse.user_data = & traverse_action;

        if (g_ctcs_api_en)
        {
            ret = ctcs_ipfix_traverse_remove(g_api_lchip, sys_goldengate_ipfix_remove_cmp, &ipfix_traverse);
        }
        else
        {
            ret = ctc_ipfix_traverse_remove(sys_goldengate_ipfix_remove_cmp, &ipfix_traverse);
        }
        if (ret < 0)
        {
            ctc_cli_out("%% ret = %d, %s \n", ret, ctc_get_error_desc(ret));
            return CLI_ERROR;
        }
    }
    return CLI_SUCCESS;
}
/**
 @brief  Initialize sdk oam module CLIs
*/
int32
ctc_goldengate_ipfix_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_status_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_clear_stats_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_delete_by_index_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_dump_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_l2_hashkey_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_l2l3_hashkey_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_ipv4_hashkey_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_mpls_hashkey_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_ipv6_hashkey_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_by_index_cmd);
	install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_set_port_cfg_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_port_cfg_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_goldengate_ipfix_remove_all_cmd);
    return CLI_SUCCESS;
}

int32
ctc_goldengate_ipfix_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_status_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_clear_stats_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_delete_by_index_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_dump_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_l2_hashkey_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_l2l3_hashkey_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_ipv4_hashkey_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_mpls_hashkey_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_ipv6_hashkey_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_by_index_cmd);
	uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_set_port_cfg_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ipfix_show_port_cfg_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_goldengate_ipfix_remove_all_cmd);
    return CLI_SUCCESS;
}

