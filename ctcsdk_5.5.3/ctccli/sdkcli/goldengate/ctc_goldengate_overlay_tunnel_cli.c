/**
 @file ctc_goldengate_overlay_tunnel_cli.c

 @date 2013-11-10

 @version v2.0

 The file apply clis of overlay tunnel module
*/

#include "sal.h"
#include "ctc_cli.h"
#include "ctc_cli_common.h"
#include "ctc_debug.h"
#include "ctc_error.h"
#include "ctc_api.h"
#include "ctcs_api.h"
#include "ctc_overlay_tunnel.h"
#include "ctc_goldengate_overlay_tunnel.h"
#include "ctc_goldengate_overlay_tunnel_cli.h"

CTC_CLI(ctc_cli_gg_overlay_tunnel_set_global_cfg,
        ctc_cli_gg_overlay_tunnel_set_global_cfg_cmd,
        "overlay-tunnel global-config (nvgre-key | vxlan-key) (ipda-ipsa-vni|ipda-vni) (uc-ipv4da-share|uc-ipv4da-individual) (vni-to-fid | fid-to-vni |)",
        "overlay tunnel module, used to terminate an overlay tunnel",
        "Global config for overlay tunnel",
        "NvGre key",
        "VxLAN/eVxLAN/GENEVE key",
        "Use ipda+ipsa+vni as decap key",
        "Use ipda+vni as decap key",
        "Use share ipda key when ipv4 unicast",
        "Use individual ipda key when ipv4 unicast",
        "vni-to-fid using 1:1 mode",
        "fid-to-vni using N:1 mode")
{
    ctc_overlay_tunnel_global_cfg_t init_param;
    uint32 index = 0xFF;
    int32 ret = CLI_ERROR;
    uint8 mode = 0;

    sal_memset(&init_param, 0 ,sizeof(init_param));

    index = CTC_CLI_GET_ARGC_INDEX("ipda-vni");
    if (0xFF != index)
    {
        mode |= CTC_OVERLAY_TUNNEL_DECAP_BY_IPDA_VNI;
    }

    index = CTC_CLI_GET_ARGC_INDEX("uc-ipv4da-individual");
    if (0xFF != index)
    {
       mode |= CTC_OVERLAY_TUNNEL_DECAP_IPV4_UC_USE_INDIVIDUL_IPDA;
    }

    index = CTC_CLI_GET_ARGC_INDEX("nvgre-key");
    if (0xFF != index)
    {
        init_param.nvgre_mode = mode;
    }
    else
    {
        init_param.vxlan_mode = mode;
    }

    index = CTC_CLI_GET_ARGC_INDEX("fid-to-vni");
    if (0xFF != index)
    {
        init_param.vni_mapping_mode = 1;
    }
    else
    {
        init_param.vni_mapping_mode = 0;
    }

    if(g_ctcs_api_en)
    {
       ret = ctcs_overlay_tunnel_init(g_api_lchip, &init_param);
    }
    else
    {
        ret = ctc_overlay_tunnel_init(&init_param);
    }

    if (ret)
    {
        ctc_cli_out("%% %s \n\r", ctc_get_error_desc(ret));
        return CLI_ERROR;
    }

    return CLI_SUCCESS;
}


int32
ctc_goldengate_overlay_tunnel_cli_init(void)
{
    install_element(CTC_INTERNAL_MODE, &ctc_cli_gg_overlay_tunnel_set_global_cfg_cmd);

    return CLI_SUCCESS;
}

int32
ctc_goldengate_overlay_tunnel_cli_deinit(void)
{
    uninstall_element(CTC_INTERNAL_MODE, &ctc_cli_gg_overlay_tunnel_set_global_cfg_cmd);

    return CLI_SUCCESS;
}

