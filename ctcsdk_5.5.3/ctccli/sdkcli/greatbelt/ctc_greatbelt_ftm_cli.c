/**
 @file ctc_greatbelt_ftm_cli.c

 @date 2009-11-23

 @version v2.0

---file comments----
*/

#include "sal.h"
#include "ctc_cli.h"
#include "ctc_ftm_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_alloc.h"
#include "ctc_error.h"
#include "ctc_debug.h"
#include "ctc_cli_common.h"
#include "sys_greatbelt_opf.h"

extern int32 sys_greatbelt_ftm_show_alloc_info(uint8 lchip);

extern int32 sys_greatbelt_ftm_adjust_tcam(uint8 lchip, uint32 acl_tcam_size[]);

/* this data structure is related with enum sys_greatbelt_opf_type */
char ctc_greatbelt_opf_str[][40] =
{
    "opf-vlan-profile",
    "opf-usrid-vlan-key",
    "opf-usrid-mac-key",
    "opf-usrid-ipv4-key",
    "opf-usrid-ipv6-key",
    "opf-usrid-ad",
    "opf-scl-vlan-key",
    "opf-scl-mac-key",
    "opf-scl-ipv4-key",
    "opf-scl-ipv6-key",
    "opf-scl-tunnel-ipv4-key",
    "opf-scl-tunnel-ipv6-key",
    "opf-scl-ad",
    "opf-scl-vlan-action",
    "opf-scl-entry-id",
    "opf-acl-ad",
    "fdb-sram-hash-collision-key",
    "fdb-ad-alloc",
    "opf-nh-dyn1-sram",
    "opf-nh-dyn2-sram",
    "opf-nh-dyn3-sram",
    "opf-nh-dyn4-sram",
    "opf-nh-nhid-internal",
    "opf-nh-dsmet-for-ipmcbitmap",
    "opf-qos-flow-policer",
    "opf-qos-policer-profile0",
    "opf-qos-policer-profile1",
    "opf-qos-policer-profile2",
    "opf-qos-policer-profile3",
    "flow-stats-sram",
    "opf-stats-statsid",
    "opf-queue-drop-profile",
    "opf-queue-shape-profile",
    "opf-group-shape-profile",
    "opf-queue-group",
    "opf-queue",
    "opf-service-queue",
    "opf-oam-chan",
    "opf-oam-tcam-key",
    "opf-oam-mep-lmep",
    "opf-oam-mep-bfd",
    "opf-oam-ma",
    "opf-oam-ma-name",
    "opf-oam-lm",
    "opf-ipv4-uc-block",
    "opf-ipv6-uc-block",
    "opf-ipv4-mc-block",
    "opf-ipv6-mc-block",
    "opf-foam-mep",
    "opf-foam-ma",
    "opf-lpm-sram0",
    "opf-lpm-sram1",
    "opf-lpm-sram2",
    "opf-lpm-sram3",
    "opf-rpf",
    "opf-ftm",
    "opf-storm-ctl",
    "opf-tunnel-ipv4-sa",
    "opf-tunnel-ipv6-ip",
    "opf-tunnel-ipv4-ipuc",
    "opf-tunnel-ipv6-ipuc",
    "opf-foam-ma-name",
    "opf-ipv4-based-l2-mc-block",
    "opf-ipv6-based-l2-mc-block",
    "opf-ipv4-nat",
    "opf-stacking",
/*end*/};

CTC_CLI(ctc_cli_gb_ftm_show_opf_type_info,
        ctc_cli_gb_ftm_show_opf_type_info_cmd,
        "show opf type (TYPE|all)",
        CTC_CLI_SHOW_STR,
        "Offset pool freelist",
        "OPF type",
        "OPF type value",
        "All types")
{
    uint8 opf_type;

    if (0 == sal_memcmp("all", argv[0], 3))
    {
        ctc_cli_out("-----------------------------------\n");

        for (opf_type = 0; opf_type < MAX_OPF_TBL_NUM; opf_type++)
        {
            ctc_cli_out("%d ---- %s\n", opf_type, ctc_greatbelt_opf_str[opf_type]);
        }
    }
    else
    {
        CTC_CLI_GET_UINT8_RANGE("opf type", opf_type, argv[0], 0, CTC_MAX_UINT8_VALUE);
        if (opf_type >= MAX_OPF_TBL_NUM)
        {
            ctc_cli_out("invalid opf type : %d \n", opf_type);
            return CLI_ERROR;
        }

        ctc_cli_out("-----------------------------------\n");
        ctc_cli_out("%d ---- %s\n", opf_type, ctc_greatbelt_opf_str[opf_type]);
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gb_ftm_show_opf_alloc_info,
        ctc_cli_gb_ftm_show_opf_alloc_info_cmd,
        "show opf alloc type TYPE index INDEX (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        "Offset pool freelist",
        "OPF alloc info",
        "OPF type",
        "OPF type value",
        "Index of opf pool",
        "<0-0xFF>",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    sys_greatbelt_opf_t opf;
    uint8 index = 0;
    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }


    lchip = g_ctcs_api_en?g_api_lchip:lchip;

    sal_memset(&opf, 0, sizeof(sys_greatbelt_opf_t));

    CTC_CLI_GET_UINT8_RANGE("opf type", opf.pool_type, argv[0], 0, CTC_MAX_UINT8_VALUE);
    CTC_CLI_GET_UINT8_RANGE("opf index", opf.pool_index, argv[1], 0, CTC_MAX_UINT8_VALUE);

    if (opf.pool_type >= MAX_OPF_TBL_NUM)
    {
        ctc_cli_out("invalid opf type : %d \n", opf.pool_type);
        return CLI_ERROR;
    }

    ctc_cli_out("Print opf type:%d -- %s index :%d alloc information\n", opf.pool_type, ctc_greatbelt_opf_str[opf.pool_type], opf.pool_index);
    ctc_cli_out("--------------------------------------------------------\n");

    ret = sys_greatbelt_opf_print_alloc_info(lchip, &opf);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_ftm_show_ftm_info,
        ctc_cli_gb_ftm_show_ftm_info_cmd,
        "show ftm info (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_MEM_ALLOCM_STR,
        "Information of TCAM and Sram",
        "show detail ftm info",
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
    ret = sys_greatbelt_ftm_show_alloc_info(lchip);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gb_ftm_adjust_resource,
        ctc_cli_gb_ftm_adjust_resource_cmd,
        "ftm adjust-resource KEY_SIZE0 KEY_SIZE1 KEY_SIZE2 KEY_SIZE3 KEY_SIZE4 KEY_SIZE5 (lchip LCHIP|)",
        CTC_CLI_MEM_ALLOCM_STR,
        "Adjust resource",
        "ACL_IPV6_KEY0",
        "ACL_IPV6_KEY1",
        "ACL_MAC_KEY0",
        "ACL_MAC_KEY1",
        "ACL_MAC_KEY2",
        "ACL_MAC_KEY3",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint32 acl_tcam_size[6] = {0};

    CTC_CLI_GET_UINT32("size", acl_tcam_size[0], argv[0]);
    CTC_CLI_GET_UINT32("size", acl_tcam_size[1], argv[1]);
    CTC_CLI_GET_UINT32("size", acl_tcam_size[2], argv[2]);
    CTC_CLI_GET_UINT32("size", acl_tcam_size[3], argv[3]);
    CTC_CLI_GET_UINT32("size", acl_tcam_size[4], argv[4]);
    CTC_CLI_GET_UINT32("size", acl_tcam_size[5], argv[5]);

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    ret = sys_greatbelt_ftm_adjust_tcam(lchip, acl_tcam_size);

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}


int32
ctc_greatbelt_ftm_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_show_opf_type_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_show_opf_alloc_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_show_ftm_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_adjust_resource_cmd);

    return CTC_E_NONE;
}

int32
ctc_greatbelt_ftm_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_show_opf_type_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_show_opf_alloc_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_show_ftm_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gb_ftm_adjust_resource_cmd);

    return CTC_E_NONE;
}

