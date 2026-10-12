/**
 @file ctc_goldengate_overlay_tunnel.c

 @date 2011-11-07

 @version v2.0


*/
/****************************************************************************
 *
* Header Files
*
****************************************************************************/
#include "ctc_const.h"
#include "ctc_error.h"

#include "ctc_overlay_tunnel.h"
#include "sys_goldengate_overlay_tunnel.h"
#include "sys_goldengate_chip.h"

/****************************************************************************
 *
* Defines and Macros
*
*****************************************************************************/

/****************************************************************************
 *
* Global and Declaration
*
*****************************************************************************/
/****************************************************************************
 *
* Function
*
*****************************************************************************/

int32
ctc_goldengate_overlay_tunnel_init(uint8 lchip, void* overlay_tunnel_global_cfg)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    ctc_overlay_tunnel_global_cfg_t global_cfg;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    if (NULL == overlay_tunnel_global_cfg)
    {
        sal_memset(&global_cfg, 0, sizeof(ctc_overlay_tunnel_global_cfg_t));
    }
    else
    {
        sal_memcpy(&global_cfg, overlay_tunnel_global_cfg, sizeof(ctc_overlay_tunnel_global_cfg_t));
    }

    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_init(lchip, &global_cfg));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_overlay_tunnel_deinit(uint8 lchip)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_deinit(lchip));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_overlay_tunnel_set_fid(uint8 lchip, uint32 vn_id, uint16 fid )
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_set_fid(lchip, vn_id, fid));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_overlay_tunnel_get_fid(uint8 lchip, uint32 vn_id, uint16* p_fid )
{
    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_get_fid (lchip,  vn_id, p_fid ));

    return CTC_E_NONE;
}

int32
ctc_goldengate_overlay_tunnel_get_vn_id(uint8 lchip,  uint16 fid, uint32* p_vn_id)
{
    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_get_vn_id(lchip, fid, p_vn_id));

    return CTC_E_NONE;
}


int32
ctc_goldengate_overlay_tunnel_add_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;
    int32 ret = CTC_E_NONE;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_FOREACH_ERROR_RETURN(CTC_E_NONE,
            ret,
            sys_goldengate_overlay_tunnel_add_tunnel(lchip, p_tunnel_param));
    }

    /*rollback if error exist*/
    CTC_FOREACH_ROLLBACK(lchip_start,lchip_end)
    {
        sys_goldengate_overlay_tunnel_remove_tunnel(lchip, p_tunnel_param);
    }

    return ret;
}

int32
ctc_goldengate_overlay_tunnel_remove_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_remove_tunnel(lchip, p_tunnel_param));
    }

    return CTC_E_NONE;
}

int32
ctc_goldengate_overlay_tunnel_update_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    FEATURE_SUPPORT_CHECK(CTC_FEATURE_OVERLAY);
    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_update_tunnel(lchip, p_tunnel_param));
    }

    return CTC_E_NONE;
}

/*add by liuhq@2019.11.26 set vxlan udp port*/
int32 ctc_goldengate_overlay_tunnel_set_vxlan_udp_port(uint8 lchip, uint16 vxlan_udp_port)
{
    uint8 lchip_start = 0;
    uint8 lchip_end = 0;

    LCHIP_CHECK(lchip);
    CTC_FOREACH_LCHIP(lchip_start, lchip_end, 1)
    {
        CTC_ERROR_RETURN(sys_goldengate_overlay_tunnel_set_vxlan_udp_port(lchip, vxlan_udp_port));
    }

    return CTC_E_NONE;
}


