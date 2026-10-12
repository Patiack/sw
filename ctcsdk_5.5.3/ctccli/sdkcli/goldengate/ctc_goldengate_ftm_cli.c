/**
 @file ctc_goldengate_ftm_cli.c

 @date 2009-11-23

 @version v2.0

---file comments----
*/

#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_ftm_cli.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_alloc.h"
#include "ctc_error.h"
#include "ctc_debug.h"

extern int32 sys_goldengate_opf_print_range_alloc_info(uint8 lchip, uint8 start_pool_type, uint8 end_pool_type);
extern int32 sys_goldengate_opf_print_alloc_info(uint8 lchip, uint8 pool_type, uint8 pool_index);
extern int32 sys_goldengate_ftm_show_alloc_info_detail(uint8 lchip);
extern int32 sys_goldengate_ftm_show_alloc_info(uint8 lchip);

/* this data structure is related with enum sys_goldengate_opf_type */
char ctc_goldengate_opf_str[][40] =
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
    "opf-acl-vlan-action",
    "fdb-sram-hash-collision-key",
    "fdb-ad-alloc",
    "opf-nh-dyn-sram",
    "opf-qos-flow-policer",
    "opf-qos-policer-profile",
    "flow-stats-sram",
    "opf-stats-statsid",
    "opf-queue-drop-profile",
    "opf-queue-shape-profile",
    "opf-group-shape-profile",
    "opf-queue-group",
    "opf-queue-exception-stacking-profile",
    "opf-service-queue",
    "opf-oam-chan",
    "opf-oam-tcam-key",
    "opf-oam-mep-lmep",
    "opf-oam-mep-bfd",
    "opf-oam-ma",
    "opf-oam-ma-name",
    "opf-oam-lm-profile",
    "opf-oam-lm",
    "opf-ipv4-uc-block",
    "opf-ipv6-uc-block",
    "opf-ipv4-mc-pointer-block",
    "opf-ipv6-mc-pointer-block",
    "opf-ipv4-l2mc-pointer-block",
    "opf-ipv6-l2mc-pointer-block",
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
    "opf-mpls",
    "opf-black-hole-cam",
    "opf-overlay-tunnel-inner-fid",
    "opf-security-learn-limit-thrhold",
    "opf-ecmp-group-id",
    "opf-ecmp-member-id",
    "opf-stacking",
    "opf-router-mac",
/*end*/};

CTC_CLI(ctc_cli_gg_ftm_show_opf_type_info,
        ctc_cli_gg_ftm_show_opf_type_info_cmd,
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

        for (opf_type = 0; opf_type < sizeof(ctc_goldengate_opf_str)/sizeof(ctc_goldengate_opf_str[0]); opf_type++)
        {
            ctc_cli_out("%d ---- %s\n", opf_type, ctc_goldengate_opf_str[opf_type]);
        }
    }
    else
    {
        CTC_CLI_GET_UINT8_RANGE("opf type", opf_type, argv[0], 0, CTC_MAX_UINT8_VALUE);
        if (opf_type >= sizeof(ctc_goldengate_opf_str)/sizeof(ctc_goldengate_opf_str[0]))
        {
            ctc_cli_out("invalid opf type : %d \n", opf_type);
            return CLI_ERROR;
        }

        ctc_cli_out("-----------------------------------\n");
        ctc_cli_out("%d ---- %s\n", opf_type, ctc_goldengate_opf_str[opf_type]);
    }

    return CLI_SUCCESS;
}

CTC_CLI(ctc_cli_gg_ftm_show_opf_alloc_info,
        ctc_cli_gg_ftm_show_opf_alloc_info_cmd,
        "show opf alloc type TYPE (end-type TYPE|index INDEX)",
        CTC_CLI_SHOW_STR,
        "Offset pool freelist",
        "OPF alloc info",
        "OPF type",
        "OPF type value",
        "OPF type",
        "OPF type value",
        "Index of opf pool",
        "<0-0xFF>")
{
    int32 ret = CLI_SUCCESS;
    uint8 lchip = 0;
    uint8 index = 0;
    uint8 start_type = 0;
    uint8 end_type = 0;
    uint8 pool_index = 0;

    lchip = g_ctcs_api_en?g_api_lchip:lchip;
    CTC_CLI_GET_UINT8_RANGE("opf type", start_type, argv[0], 0, CTC_MAX_UINT8_VALUE);

    index = CTC_CLI_GET_ARGC_INDEX("end-type");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("end-type", end_type, argv[index+1], 0, CTC_MAX_UINT8_VALUE);

        ctc_cli_out("Print opf type: %s(%d) ~ %s(%d) alloc information\n",
                ctc_goldengate_opf_str[start_type], start_type, ctc_goldengate_opf_str[end_type], end_type);
        ctc_cli_out("--------------------------------------------------------\n");

        ret = sys_goldengate_opf_print_range_alloc_info(lchip, start_type, end_type);
    }

    index = CTC_CLI_GET_ARGC_INDEX("index");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8_RANGE("index", pool_index, argv[index+1], 0, CTC_MAX_UINT8_VALUE);

        ctc_cli_out("Print opf type: %s(%d) index: %d alloc information\n",
                        ctc_goldengate_opf_str[start_type], start_type, pool_index);
        ctc_cli_out("--------------------------------------------------------\n");

        ret = sys_goldengate_opf_print_alloc_info(lchip, start_type, pool_index);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

CTC_CLI(ctc_cli_gg_ftm_show_ftm_info,
        ctc_cli_gg_ftm_show_ftm_info_cmd,
        "show ftm info (detail|) (lchip LCHIP|)",
        CTC_CLI_SHOW_STR,
        CTC_CLI_MEM_ALLOCM_STR,
        "Information of TCAM and Sram",
        "show detail ftm info",
        CTC_CLI_LCHIP_ID_STR,
        CTC_CLI_LCHIP_ID_VALUE)
{
    int32 ret = CLI_SUCCESS;
    uint8 index = 0;
    uint8 lchip = 0;

    index = CTC_CLI_GET_ARGC_INDEX("lchip");
    if (0xFF != index)
    {
        CTC_CLI_GET_UINT8("lchip", lchip, argv[index + 1]);
    }
    lchip = g_ctcs_api_en? g_api_lchip : lchip;
    index = CTC_CLI_GET_ARGC_INDEX("detail");
    if (0xFF != index)
    {
        ret = sys_goldengate_ftm_show_alloc_info_detail(lchip);
    }
    else
    {
        ret = sys_goldengate_ftm_show_alloc_info(lchip);
    }

    if (ret < 0)
    {
        ctc_cli_out("%% %s \n", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return ret;
}

int32
ctc_goldengate_ftm_cli_init(void)
{
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ftm_show_opf_type_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ftm_show_opf_alloc_info_cmd);
    install_element(CTC_SDK_MODE, &ctc_cli_gg_ftm_show_ftm_info_cmd);

    return CTC_E_NONE;
}

int32
ctc_goldengate_ftm_cli_deinit(void)
{
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ftm_show_opf_type_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ftm_show_opf_alloc_info_cmd);
    uninstall_element(CTC_SDK_MODE, &ctc_cli_gg_ftm_show_ftm_info_cmd);

    return CTC_E_NONE;
}

