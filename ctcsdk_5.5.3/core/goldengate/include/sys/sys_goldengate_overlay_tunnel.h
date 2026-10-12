/**
 @file sys_goldengate_overlay_tunnel.h

 @date 2013-10-25

 @version v2.0

*/

#ifndef _SYS_GOLDENGATE_OVERLAY_TUNNEL_H
#define _SYS_GOLDENGATE_OVERLAY_TUNNEL_H
#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************
 *
 * Header Files
 *
 ***************************************************************/
#include "sal.h"

#include "ctc_const.h"
#include "ctc_overlay_tunnel.h"
#include "ctc_debug.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define SYS_OVERLAY_TUNNEL_DBG_OUT(level, FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT(overlay_tunnel, overlay_tunnel, OVERLAY_TUNNEL_SYS, level, FMT, ##__VA_ARGS__);  \
    } while (0)

#define SYS_OVERLAY_TUNNEL_DBG_FUNC()                          \
    { \
    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_FUNC, "%s()\n",__FUNCTION__);\
    }

#define SYS_OVERLAY_TUNNEL_DBG_DUMP(FMT, ...)               \
    {                                                 \
    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_DUMP, FMT, ##__VA_ARGS__);\
    }

#define SYS_OVERLAY_TUNNEL_DBG_INFO(FMT, ...) \
    { \
    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_INFO, FMT, ##__VA_ARGS__);\
    }

#define SYS_OVERLAY_TUNNEL_DBG_ERROR(FMT, ...) \
    { \
    SYS_OVERLAY_TUNNEL_DBG_OUT(CTC_DEBUG_LEVEL_ERROR, FMT, ##__VA_ARGS__);\
    }

/****************************************************************************
*
* Function
*
*****************************************************************************/
extern int32
sys_goldengate_overlay_tunnel_init(uint8 lchip, void* o_param);

/**
 @brief De-Initialize overlay_tunnel module
*/
extern int32
sys_goldengate_overlay_tunnel_deinit(uint8 lchip);

extern int32
sys_goldengate_overlay_tunnel_set_fid (uint8 lchip,  uint32 vn_id, uint16 fid );

extern int32
sys_goldengate_overlay_tunnel_get_fid (uint8 lchip,  uint32 vn_id, uint16* fid );

extern int32
sys_goldengate_overlay_tunnel_get_vn_id(uint8 lchip, uint16 fid, uint32* p_vn_id);

extern int32
sys_goldengate_overlay_tunnel_add_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param);

extern int32
sys_goldengate_overlay_tunnel_remove_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param);

extern int32
sys_goldengate_overlay_tunnel_update_tunnel(uint8 lchip, ctc_overlay_tunnel_param_t* p_tunnel_param);

extern int32
sys_goldengate_overlay_tunnel_set_vxlan_udp_port(uint8 lchip, uint16 vxlan_udp_port);

#ifdef __cplusplus
}
#endif

#endif

