#include "sal.h"
#include "ctc_cli.h"
#include "ctc_debug.h"
#include "ctc_hash.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_ipuc.h"
#include "ctc_cli_common.h"
#include "sys_greatbelt_chip.h"
#include "sys_greatbelt_ipmc.h"
#include "sys_greatbelt_ipmc_db.h"

extern int32
sys_greatbelt_ipmc_db_show_group_info(uint8 lchip, uint8 ip_version);

extern int32
sys_greatbelt_ipmc_db_show_member_info(uint8 lchip, ctc_ipmc_group_info_t* p_group);

extern int32
sys_greatbelt_ipmc_db_show_count(uint8 lchip);

extern int32
sys_greatbelt_ipmc_block_show(uint8 lchip, uint8 blockid, uint8 detail);

 static ctc_ipmc_group_info_t ipmc_group_tmp;

CTC_CLI(ctc_cli_gb_ipmc_show_ipv4_group_info,
        ctc_cli_gb_ipmc_show_ipv4_group_info_cmd,
        "show ipmc group info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "IP multicast group",
        "Information, including source and group IP adress, etc.",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 ip_version = 0;
    uint8 lchip = 0;
    int32 ret = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    ip_version = CTC_IP_VER_4;

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_db_show_group_info(lchip,ip_version);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_ipv6_group_info,
        ctc_cli_gb_ipmc_show_ipv6_group_info_cmd,
        "show ipmc ipv6 group info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "IPv6",
        CTC_CLI_IPMC_M_STR,
        "IP multicast group",
        "Information, including source and group IP adress, etc.",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    uint8 ip_version = 0;
    uint8 lchip = 0;
    int32 ret = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    ip_version = CTC_IP_VER_6;
    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_db_show_group_info(lchip, ip_version);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_ipv4_group_param,
        ctc_cli_gb_ipmc_show_ipv4_group_param_cmd,
        "show ipmc group info group-address A.B.C.D MASK_LEN source-address A.B.C.D MASK_LEN vrf VRFID (ip-based-l2mc|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "IP multicast group",
        "Information, including source and group IP adress, etc.",
        "Group address, class D IP address",
        CTC_CLI_IPV4_FORMAT,
        "The length of group address mask <32>",
        "Source address",
        CTC_CLI_IPV4_FORMAT,
        "The length of source address mask <0 or 32>",
        CTC_CLI_VRFID_DESC,
        CTC_CLI_VRFID_ID_DESC,
        "Do IP based L2 mcast")
{
    int32 ret  = CLI_SUCCESS;
    uint32 masklen = 0;
    uint16 vrf_id = 0;
    uint8  index = 0;
    ctc_ipmc_group_info_t* ipmc_group_info = &ipmc_group_tmp;

    sal_memset(ipmc_group_info, 0, sizeof(ctc_ipmc_group_info_t));

    ipmc_group_info->ip_version = CTC_IP_VER_4;

    CTC_CLI_GET_IPV4_ADDRESS("ip", ipmc_group_info->address.ipv4.group_addr, argv[0]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[1], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->group_ip_mask_len = masklen;

    CTC_CLI_GET_IPV4_ADDRESS("ip", ipmc_group_info->address.ipv4.src_addr, argv[2]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[3], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->src_ip_mask_len = masklen;

    CTC_CLI_GET_UINT16_RANGE("vrf", vrf_id, argv[4], 0, CTC_MAX_UINT16_VALUE);
    ipmc_group_info->address.ipv4.vrfid = vrf_id;

    index = CTC_CLI_GET_ARGC_INDEX("ip-based-l2mc");
    if (index != 0xFF)
    {
        ipmc_group_info->flag |= CTC_IPMC_FLAG_L2MC;
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_ipmc_get_group_info(g_api_lchip, ipmc_group_info);
    }
    else
    {
        ret = ctc_ipmc_get_group_info(ipmc_group_info);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("===================================\n");
    ctc_cli_out("ipmc group info                    \n");
    ctc_cli_out("groupid                     :0x%x\n", ipmc_group_info->group_id);
    ctc_cli_out("flag                        :0x%x\n", ipmc_group_info->flag);

    for (index = 0; index <= 15; index++)
    {
        if (ipmc_group_info->rpf_intf_valid[index] == 1)
        {
            ctc_cli_out("rpf interface id            :0x%x\n", ipmc_group_info->rpf_intf[index]);
            ctc_cli_out("rpf interface valid         :%d\n", ipmc_group_info->rpf_intf_valid[index]);
        }
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_ipv6_group_param,
        ctc_cli_gb_ipmc_show_ipv6_group_param_cmd,
        "show ipmc ipv6 group info group-address X:X::X:X MASK_LEN source-address X:X::X:X MASK_LEN vrf VRFID (ip-based-l2mc|)",
        CTC_CLI_SHOW_STR,
        "IPv6",
        CTC_CLI_IPMC_M_STR,
        "IP multicast group",
        "Information, including source and group IP adress, etc.",
        "Group address, IPv6 address begin with FF",
        CTC_CLI_IPV6_FORMAT,
        "The length of group address mask <128>",
        "Source address",
        CTC_CLI_IPV6_FORMAT,
        "The length of source address mask <0 or 128>",
        CTC_CLI_VRFID_DESC,
        CTC_CLI_VRFID_ID_DESC,
        "Do IP based L2 mcast")
{
    int32 ret  = CLI_SUCCESS;
    uint32 masklen = 0;
    uint16 vrf_id = 0;
    uint8  index = 0;
    uint32 ipv6_address[4] = {0, 0, 0, 0};

    ctc_ipmc_group_info_t* ipmc_group_info = &ipmc_group_tmp;

    sal_memset(ipmc_group_info, 0, sizeof(ctc_ipmc_group_info_t));

    ipmc_group_info->ip_version = CTC_IP_VER_6;

    CTC_CLI_GET_IPV6_ADDRESS("ipv6", ipv6_address, argv[0]);

    /* adjust endian */
    ipmc_group_info->address.ipv6.group_addr[0] = sal_htonl(ipv6_address[0]);
    ipmc_group_info->address.ipv6.group_addr[1] = sal_htonl(ipv6_address[1]);
    ipmc_group_info->address.ipv6.group_addr[2] = sal_htonl(ipv6_address[2]);
    ipmc_group_info->address.ipv6.group_addr[3] = sal_htonl(ipv6_address[3]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[1], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->group_ip_mask_len = masklen;

    CTC_CLI_GET_IPV6_ADDRESS("ipv6", ipv6_address, argv[2]);

    /* adjust endian */
    ipmc_group_info->address.ipv6.src_addr[0] = sal_htonl(ipv6_address[0]);
    ipmc_group_info->address.ipv6.src_addr[1] = sal_htonl(ipv6_address[1]);
    ipmc_group_info->address.ipv6.src_addr[2] = sal_htonl(ipv6_address[2]);
    ipmc_group_info->address.ipv6.src_addr[3] = sal_htonl(ipv6_address[3]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[3], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->src_ip_mask_len = masklen;

    CTC_CLI_GET_UINT16_RANGE("vrf", vrf_id, argv[4], 0, CTC_MAX_UINT16_VALUE);
    ipmc_group_info->address.ipv6.vrfid = vrf_id;

    index = CTC_CLI_GET_ARGC_INDEX("ip-based-l2mc");
    if (index != 0xFF)
    {
        ipmc_group_info->flag |= CTC_IPMC_FLAG_L2MC;
    }

    if(g_ctcs_api_en)
    {
        ret = ctcs_ipmc_get_group_info(g_api_lchip, ipmc_group_info);
    }
    else
    {
        ret = ctc_ipmc_get_group_info(ipmc_group_info);
    }
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    ctc_cli_out("===================================\n");
    ctc_cli_out("ipmc group info                    \n");
    ctc_cli_out("groupid                     :0x%x\n", ipmc_group_info->group_id);
    ctc_cli_out("flag                        :0x%x\n", ipmc_group_info->flag);
    for (index = 0; index <= 15; index++)
    {
        if (ipmc_group_info->rpf_intf_valid[index] == 1)
        {
            ctc_cli_out("rpf interface id            :0x%x\n", ipmc_group_info->rpf_intf[index]);
            ctc_cli_out("rpf interface valid         :%d\n", ipmc_group_info->rpf_intf_valid[index]);
        }
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_ipv4_member_info,
        ctc_cli_gb_ipmc_show_ipv4_member_info_cmd,
        "show ipmc member info group-address A.B.C.D MASK_LEN source-address A.B.C.D MASK_LEN vrf VRFID (ip-based-l2mc|)"\
        "(lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "Members of a given multicast group",
        "Information, including gport, vlan, l3 type.",
        "Group address, class D IP address",
        CTC_CLI_IPV4_FORMAT,
        "The length of group address mask <32>",
        "Source address",
        CTC_CLI_IPV4_FORMAT,
        "The length of source address mask <0 or 32>",
        CTC_CLI_VRFID_DESC,
        CTC_CLI_VRFID_ID_DESC,
        "Do IP based L2 mcast",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{

    int32 ret  = CLI_SUCCESS;
    uint32 masklen = 0;
    uint16 vrf_id = 0;
    uint8 index = 0;
    uint8 lchip = 0;
    ctc_ipmc_group_info_t* ipmc_group_info = &ipmc_group_tmp;

    sal_memset(ipmc_group_info, 0, sizeof(ctc_ipmc_group_info_t));
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    ipmc_group_info->ip_version = CTC_IP_VER_4;

    CTC_CLI_GET_IPV4_ADDRESS("ip", ipmc_group_info->address.ipv4.group_addr, argv[0]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[1], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->group_ip_mask_len = masklen;

    CTC_CLI_GET_IPV4_ADDRESS("ip", ipmc_group_info->address.ipv4.src_addr, argv[2]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[3], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->src_ip_mask_len = masklen;

    CTC_CLI_GET_UINT16_RANGE("vrf", vrf_id, argv[4], 0, CTC_MAX_UINT16_VALUE);
    ipmc_group_info->address.ipv4.vrfid = vrf_id;

    index = CTC_CLI_GET_ARGC_INDEX("ip-based-l2mc");
    if (index != 0xFF)
    {
        ipmc_group_info->flag |= CTC_IPMC_FLAG_L2MC;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_db_show_member_info(lchip, ipmc_group_info);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_ipv6_member_info,
        ctc_cli_gb_ipmc_show_ipv6_member_info_cmd,
        "show ipmc ipv6 member info group-address X:X::X:X MASK_LEN source-address X:X::X:X MASK_LEN vrf VRFID (ip-based-l2mc|)"\
        "(lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "IPv6",
        "Members of a given multicast group",
        "Information, including gport, vlan, l3 type.",
        "Group address, IPv6 address begin with FF",
        CTC_CLI_IPV6_FORMAT,
        "The length of group address mask <128>",
        "Source address",
        CTC_CLI_IPV6_FORMAT,
        "The length of source address mask <0 or 128>",
        CTC_CLI_VRFID_DESC,
        CTC_CLI_VRFID_ID_DESC,
        "Do IP based L2 mcast",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint32 masklen = 0;
    uint8 lchip = 0;
    uint16 vrf_id = 0;
    uint8  index = 0;
    uint32 ipv6_address[4] = {0, 0, 0, 0};

    ctc_ipmc_group_info_t* ipmc_group_info = &ipmc_group_tmp;

    sal_memset(ipmc_group_info, 0, sizeof(ctc_ipmc_group_info_t));

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    ipmc_group_info->ip_version = CTC_IP_VER_6;

    CTC_CLI_GET_IPV6_ADDRESS("ipv6", ipv6_address, argv[0]);

    /* adjust endian */
    ipmc_group_info->address.ipv6.group_addr[0] = sal_htonl(ipv6_address[0]);
    ipmc_group_info->address.ipv6.group_addr[1] = sal_htonl(ipv6_address[1]);
    ipmc_group_info->address.ipv6.group_addr[2] = sal_htonl(ipv6_address[2]);
    ipmc_group_info->address.ipv6.group_addr[3] = sal_htonl(ipv6_address[3]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[1], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->group_ip_mask_len = masklen;

    CTC_CLI_GET_IPV6_ADDRESS("ipv6", ipv6_address, argv[2]);

    /* adjust endian */
    ipmc_group_info->address.ipv6.src_addr[0] = sal_htonl(ipv6_address[0]);
    ipmc_group_info->address.ipv6.src_addr[1] = sal_htonl(ipv6_address[1]);
    ipmc_group_info->address.ipv6.src_addr[2] = sal_htonl(ipv6_address[2]);
    ipmc_group_info->address.ipv6.src_addr[3] = sal_htonl(ipv6_address[3]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[3], 0, CTC_MAX_UINT8_VALUE);
    ipmc_group_info->src_ip_mask_len = masklen;

    CTC_CLI_GET_UINT16_RANGE("vrf", vrf_id, argv[4], 0, CTC_MAX_UINT16_VALUE);
    ipmc_group_info->address.ipv6.vrfid = vrf_id;

    index = CTC_CLI_GET_ARGC_INDEX("ip-based-l2mc");
    if (index != 0xFF)
    {
        ipmc_group_info->flag |= CTC_IPMC_FLAG_L2MC;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_db_show_member_info(lchip, ipmc_group_info);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_count,
        ctc_cli_gb_ipmc_show_count_cmd,
        "show ipmc count (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "Show ipmc route number",
         CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_db_show_count(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipmc_show_ipmc_status,
        ctc_cli_gb_ipmc_show_ipmc_status_cmd,
        "show ipmc status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "Ipmc and L2mc based ip Status",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret  = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_show_status(lchip);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }
    return ret;

}

CTC_CLI(ctc_cli_gb_ipmc_offset_show,
        ctc_cli_gb_ipmc_offset_show_cmd,
        "show ipmc offset (block-id INDEX|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPMC_M_STR,
        "IPMC offset",
        "Block id",
        "Block index",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_ERROR;
    uint32 index = 0;
    uint8 block_id = 0;
    uint8 lchip = 0;
    uint8 detail = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("block-id");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("block id", block_id, argv[index + 1]);
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipmc_block_show(lchip, block_id, detail);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


int32
ctc_greatbelt_ipmc_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv4_group_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv4_group_param_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv4_member_info_cmd);

    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv6_group_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv6_group_param_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv6_member_info_cmd);

    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_count_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipmc_status_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_ipmc_offset_show_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_ipmc_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv4_group_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv4_group_param_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv4_member_info_cmd);

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv6_group_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv6_group_param_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipv6_member_info_cmd);

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_count_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipmc_show_ipmc_status_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_ipmc_offset_show_cmd);

    return CLI_SUCCESS;
}

