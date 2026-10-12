/**
 @file ctc_ipuc_cli.c

 @date 2009-12-30

 @version v2.0

 The file apply clis of ipuc module
*/

#include "sal.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_ipuc_cli.h"
#include "ctc_api.h"
#include "ctc_ipuc.h"
#include "ctcs_api.h"


extern int32
sys_greatbelt_ipuc_db_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint32 detail);
extern int32
sys_greatbelt_ipuc_nat_sa_db_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint32 detail);
extern int32
sys_greatbelt_ipuc_offset_show(uint8 lchip, ctc_ip_ver_t ip_ver, uint8 blockid, uint8 detail);
extern int32
sys_greatbelt_ipuc_state_show(uint8 lchip);
extern int32
sys_greatbelt_ipuc_db_show_count(uint8 lchip);

extern int32
sys_greatbelt_ipuc_reinit(uint8 lchip, bool use_hash8);

extern int32
sys_greatbelt_l3_hash_state_show(uint8 lchip);
extern int32
sys_greatbelt_lpm_enable_dma(uint8 lchip, uint8 enable);
extern int32
sys_greatbelt_ipuc_show_debug_info(uint8 lchip, ctc_ipuc_param_t* p_ipuc_param, uint8 detail);

CTC_CLI(ctc_cli_gb_ipuc_show_ipv4,
        ctc_cli_gb_ipuc_show_ipv4_cmd,
        "show ipuc (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPUC_M_STR,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_ipuc_db_show(lchip, CTC_IP_VER_4, FALSE);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_show_ipv4_nat_sa,
        ctc_cli_gb_ipuc_show_ipv4_nat_sa_cmd,
        "show ipuc nat-sa (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPUC_M_STR,
        "Nat sa",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    uint8 lchip = 0;
    uint8  index = 0;
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en?g_api_lchip:lchip;


    ret = sys_greatbelt_ipuc_nat_sa_db_show(lchip, CTC_IP_VER_4, FALSE);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_show_ipv6,
        ctc_cli_gb_ipuc_show_ipv6_cmd,
        "show ipuc ipv6 (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPUC_M_STR,
        CTC_CLI_IPV6_STR,
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_ipuc_db_show(lchip, CTC_IP_VER_6, FALSE);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_show_count,
        ctc_cli_gb_ipuc_show_count_cmd,
        "show ipuc count (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPUC_M_STR,
        "Show ipuc route number",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipuc_db_show_count(lchip);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_show_state,
        ctc_cli_gb_ipuc_show_state_cmd,
        "show ipuc status (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPUC_M_STR,
        "IPUC configure and state",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    ret = sys_greatbelt_ipuc_state_show(lchip);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_offset_show,
        ctc_cli_gb_ipuc_offset_show_cmd,
        "show ipuc offset (ipv6 | ) (block-id INDEX|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_IPUC_M_STR,
        "IPUC offset",
        CTC_CLI_IPV6_STR,
        "Block id",
        "Block index",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32  ret = CLI_ERROR;
    uint32 index = 0;
    uint8 block_id = 0;
    uint32 ip_ver = MAX_CTC_IP_VER;
    uint8 lchip = 0;
    uint8 detail = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }


    if (0 == sal_memcmp("ipv6", argv[0], 4))
    {
        ip_ver = CTC_IP_VER_6;
    }
    else
    {
        ip_ver = CTC_IP_VER_4;
    }


    index = CTC_CLI_GET_ARGC_INDEX("block-id");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("block id", block_id, argv[index + 1]);
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipuc_offset_show(lchip, ip_ver, block_id, detail);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


CTC_CLI(ctc_cli_gb_ipuc_arrange_fragment,
        ctc_cli_gb_ipuc_arrange_fragment_cmd,
        "ipuc arrange fragment",
        CTC_CLI_IPUC_M_STR,
        "Arrange lpm fragment",
        "Arrange lpm fragment")
{
    int32 ret = CLI_ERROR;

    if(g_ctcs_api_en)
    {
        ret = ctcs_ipuc_arrange_fragment(g_api_lchip, NULL);
    }
    else
    {
        ret = ctc_ipuc_arrange_fragment(NULL);
    }
    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_reinit,
        ctc_cli_gb_ipuc_reinit_cmd,
        "ipuc reinit pipline3 (enable | disable ) (lchip LCHIP|)",
        CTC_CLI_IPUC_M_STR,
        "Reinit ipv4 module with pipline mode (test usage only)",
        "Pipline3 enable means do lpm with hash8 and 3 level pipline else means do lpm with hash16 with 2 level pipline",
        "Enable : do hash8 and 3 level pipline lookup",
        "Disable : do hash16 and 2 level pipline lookup",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_ERROR;
    bool pipline3_enable;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }


    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (index != 0xFF)
    {
        pipline3_enable = TRUE;
    }
    else
    {
        pipline3_enable = FALSE;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipuc_reinit(lchip, pipline3_enable);

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ipuc_show_l3_hash_state,
        ctc_cli_gb_ipuc_show_l3_hash_state_cmd,
        "show ipuc hash (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Show ipuc hash state",
        "Show ipuc hash state",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_l3_hash_state_show(lchip);
    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_ipuc_show_ipv4_info,
        ctc_cli_ipuc_show_ipv4_info_cmd,
        "show ipuc VRFID A.B.C.D MASK_LEN (detail |) (lchip LCHIP|)",
        CTC_CLI_IPUC_M_STR,
        "Show ipuc ipv4 route",
        CTC_CLI_VRFID_ID_DESC,
        CTC_CLI_IPV4_FORMAT,
        CTC_CLI_IPV4_MASK_LEN_FORMAT,
        "show the detail of this entry",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint32 vrfid = 0;
    uint32 masklen = 0;
    ctc_ipuc_param_t ipuc_info = {0};
    uint8 detail = 0;
    uint8 index = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    CTC_CLI_GET_UINT16_RANGE("vrfid", vrfid, argv[0], 0, CTC_MAX_UINT16_VALUE);
    ipuc_info.vrf_id = vrfid;

    CTC_CLI_GET_IPV4_ADDRESS("ip", ipuc_info.ip.ipv4, argv[1]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[2], 0, CTC_MAX_UINT8_VALUE);
    ipuc_info.masklen = masklen;

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipuc_show_debug_info(lchip, &ipuc_info,detail);


    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_ipuc_show_ipv6_info,
        ctc_cli_ipuc_show_ipv6_info_cmd,
        "show ipuc ipv6 VRFID X:X::X:X MASK_LEN (detail |) (lchip LCHIP|)",
        CTC_CLI_IPUC_M_STR,
        CTC_CLI_IPV6_STR,
        "show ipuc ipv6 route",
        CTC_CLI_VRFID_ID_DESC,
        CTC_CLI_IPV6_FORMAT,
        CTC_CLI_IPV6_MASK_LEN_FORMAT,
        "show the detail of this entry",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint32 vrfid = 0;
    uint32 masklen = 0;
    ipv6_addr_t ipv6_address;
    ctc_ipuc_param_t ipuc_info = {0};
    uint8 detail = 0;
    uint8 index = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    ipuc_info.ip_ver = CTC_IP_VER_6;

    CTC_CLI_GET_UINT16_RANGE("vrfid", vrfid, argv[0], 0, CTC_MAX_UINT16_VALUE);
    ipuc_info.vrf_id = vrfid;

    CTC_CLI_GET_IPV6_ADDRESS("ipv6", ipv6_address, argv[1]);

    /* adjust endian */
    ipuc_info.ip.ipv6[0] = sal_htonl(ipv6_address[0]);
    ipuc_info.ip.ipv6[1] = sal_htonl(ipv6_address[1]);
    ipuc_info.ip.ipv6[2] = sal_htonl(ipv6_address[2]);
    ipuc_info.ip.ipv6[3] = sal_htonl(ipv6_address[3]);

    CTC_CLI_GET_UINT8_RANGE("mask", masklen, argv[2], 0, CTC_MAX_UINT8_VALUE);
    ipuc_info.masklen = masklen;

    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (index != 0xFF)
    {
        detail = 1;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_ipuc_show_debug_info(lchip, &ipuc_info,detail);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_ipuc_write_lpm_by_dma,
        ctc_cli_ipuc_write_lpm_by_dma_cmd,
        "ipuc dma (enable|disable) (lchip LCHIP|)",
        CTC_CLI_IPUC_M_STR,
        "Use",
        "DMA",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = 0;
    bool dma_enable;
    uint8 lchip = 0;
    uint8  index = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }

    index = CTC_CLI_GET_ARGC_INDEX("enable");
    if (index != 0xFF)
    {
        dma_enable = TRUE;
    }
    else
    {
        dma_enable = FALSE;
    }

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    ret = sys_greatbelt_lpm_enable_dma(lchip, dma_enable);
    if (ret < 0)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_ipuc_cli_init(void)
{

#ifdef SDK_INTERNAL_CLI_SHOW
    install_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_ipuc_show_l3_hash_state_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gb_ipuc_offset_show_cmd);
    install_element(CTC_INTERNAL_MODE, &ctc_cli_ipuc_write_lpm_by_dma_cmd);
#endif

    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_arrange_fragment_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_ipv6_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_count_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_state_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_ipv4_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_ipv4_nat_sa_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_reinit_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_ipuc_show_ipv4_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_ipuc_show_ipv6_info_cmd);

    return CLI_SUCCESS;
}

int32
ctc_greatbelt_ipuc_cli_deinit(void)
{

#ifdef SDK_INTERNAL_CLI_SHOW
    uninstall_element(CTC_INTERNAL_MODE,  &ctc_cli_gb_ipuc_show_l3_hash_state_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gb_ipuc_offset_show_cmd);
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_ipuc_write_lpm_by_dma_cmd);
#endif

    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_arrange_fragment_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_ipv6_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_count_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_state_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_ipv4_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_show_ipv4_nat_sa_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ipuc_reinit_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_ipuc_show_ipv4_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_ipuc_show_ipv6_info_cmd);

    return CLI_SUCCESS;
}

