/**
 @file sys_goldengate_mirror.h

 @date 2009-10-21

 @version v2.0

*/

#ifndef _SYS_GOLDENGATE_MIRROR_H
#define _SYS_GOLDENGATE_MIRROR_H
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
#include "ctc_mirror.h"
#include "ctc_debug.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/

#define SYS_MIRROR_INGRESS_L2_SPAN_INDEX_BASE      0
#define SYS_MIRROR_INGRESS_L3_SPAN_INDEX_BASE      4
#define SYS_MIRROR_INGRESS_ACL_LOG_INDEX_BASE      8
#define SYS_MIRROR_EGRESS_L2_SPAN_INDEX_BASE       32
#define SYS_MIRROR_EGRESS_L3_SPAN_INDEX_BASE       36
#define SYS_MIRROR_EGRESS_ACL_LOG_INDEX_BASE       40
#define SYS_MIRROR_ACL_LOG_PRIORITY_NUM            4
#define SYS_MIRROR_ACL_LOG_ID_NUM                  4
#define SYS_MIRROR_CPU_TX_SPAN_INDEX               26
#define SYS_MIRROR_CPU_RX_SPAN_INDEX               45

enum sys_mirror_discard_e
{
    SYS_MIRROR_L2SPAN_DISCARD = 0x1,                 /**<mirror port session drop packet*/
    SYS_MIRROR_L3SPAN_DISCARD = 0x2,                 /**<mirror vlan session drop packet*/
    SYS_MIRROR_INGRESS_ACLLOG_PRI_DISCARD = 0x3C,      /**<mirror ingress acl priority 0 session drop packet*/
    SYS_MIRROR_EGRESS_ACLLOG_PRI_DISCARD = 0X4       /**<mirror egress acl priority 0 session drop packet*/
};
typedef enum sys_mirror_discard_e sys_mirror_discard_t;

#define SYS_MIRROR_DISCARD_CHECK(flag) \
    if (0 == (flag & (CTC_MIRROR_ACLLOG_PRI_DISCARD | \
                      CTC_MIRROR_L3SPAN_DISCARD | CTC_MIRROR_L2SPAN_DISCARD))) \
        return CTC_E_INVALID_PARAM

#define SYS_MIRROR_DBG_INFO(FMT, ...) \
    do \
    { \
        CTC_DEBUG_OUT_INFO(mirror, mirror, MIRROR_SYS, FMT, ##__VA_ARGS__); \
    } while (0)

#define SYS_MIRROR_DBG_OUT(level, FMT, ...)                          \
    do                                                     \
    {                                                      \
        CTC_DEBUG_OUT(mirror, mirror, MIRROR_SYS, level, FMT, ##__VA_ARGS__);  \
    } while (0)


/****************************************************************************
*
* Function
*
*****************************************************************************/
extern int32
sys_goldengate_mirror_init(uint8 lchip);

/**
 @brief De-Initialize mirror module
*/
extern int32
sys_goldengate_mirror_deinit(uint8 lchip);

extern int32
sys_goldengate_mirror_set_port_en(uint8 lchip, uint32 gport, ctc_direction_t dir, bool enable, uint8 session_id);

extern int32
sys_goldengate_mirror_get_port_info(uint8 lchip, uint16 gport, ctc_direction_t dir, bool* enable, uint8* session_id);

extern int32
sys_goldengate_mirror_set_vlan_en(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, bool enable, uint8 session_id);

extern int32
sys_goldengate_mirror_get_vlan_info(uint8 lchip, uint16 vlan_id, ctc_direction_t dir, bool* enable, uint8* session_id);

extern int32
sys_goldengate_mirror_set_dest(uint8 lchip, ctc_mirror_dest_t* mirror);

extern int32
sys_goldengate_mirror_rspan_escape_en(uint8 lchip, bool enable);

extern int32
sys_goldengate_mirror_rspan_escape_mac(uint8 lchip, ctc_mirror_rspan_escape_t* escape);

extern int32
sys_goldengate_mirror_unset_dest(uint8 lchip, ctc_mirror_dest_t* mirror);

extern int32
sys_goldengate_mirror_set_mirror_discard(uint8 lchip, ctc_direction_t dir, uint16 discard_flag, bool enable);

extern int32
sys_goldengate_mirror_get_mirror_discard(uint8 lchip, ctc_direction_t dir, ctc_mirror_discard_t discard_flag, bool* enable);

extern int32
sys_goldengate_mirror_set_erspan_psc(uint8 lchip, ctc_mirror_erspan_psc_t* psc);

extern int32
sys_goldengate_mirror_show_info(uint8 lchip, ctc_mirror_info_type_t type, uint32 value);

#ifdef __cplusplus
}
#endif

#endif

