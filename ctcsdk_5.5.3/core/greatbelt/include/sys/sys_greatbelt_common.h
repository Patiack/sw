/**
 @file sys_greatbelt_common.h

 @date 2010-3-10

 @version v2.0

*/

#ifndef _SYS_GREATBELT_COMMON_H
#define _SYS_GREATBELT_COMMON_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_const.h"

#define SYS_PKT_CPU_QDEST_BY_DMA     15*8  /*default defsub queue id = 120*/

#define SYS_REASON_ENCAP_DEST_MAP(gchip, queue_id) \
    SYS_NH_ENCODE_DESTMAP(0, gchip, ((1 << 13) | queue_id))

#define  SYS_GET_MAC_ID(lchip, gport)                          sys_greatebelt_common_get_mac_id(lchip, gport)
#define  SYS_GET_CHANNEL_ID(lchip, gport)                      sys_greatebelt_common_get_channel_id(lchip, gport)
#define  SYS_GET_LPORT_ID_WITH_MAC(lchip, mac_id)       sys_greatebelt_common_get_lport_with_mac(lchip, mac_id)
#define  SYS_GET_LPORT_ID_WITH_CHAN(lchip, chan_id)     sys_greatebelt_common_get_lport_with_chan(lchip, chan_id)
#define  SYS_MAC_IS_VALID(lchip, mac_id)                sys_greatbelt_common_check_mac_valid(lchip, mac_id)



/**** reserved local phy port ****/
#define SYS_RESERVE_PORT_ID_DROP      66 /* DRV_DATA_PATH_DROP_LPORT */
#define SYS_RESERVE_PORT_ID_ILOOP     61 /* DRV_DATA_PATH_ILOOP_LPORT */
#define SYS_RESERVE_PORT_ID_CPU       62 /* DRV_DATA_PATH_CPU_LPORT */
#define SYS_RESERVE_PORT_ID_DMA       63 /* DRV_DATA_PATH_DMA_LPORT */
#define SYS_RESERVE_PORT_ID_OAM       64 /* DRV_DATA_PATH_OAM_LPORT */
#define SYS_RESERVE_PORT_ID_ELOOP     65 /* DRV_DATA_PATH_ELOOP_LPORT */
#define SYS_RESERVE_PORT_ID_INTLK     60 /* DRV_DATA_PATH_INTLK_LPORT */


#define SYS_RESERVE_PORT_IP_TUNNEL    67
#define SYS_RESERVE_PORT_MPLS_BFD     68
#define SYS_RESERVE_PORT_PW_VCCV_BFD  69
#define SYS_RESERVE_PORT_MIRROR       SYS_RESERVE_PORT_ID_ILOOP
#define SYS_RESERVE_PORT_ID_CPU_REMOTE 70
#define SYS_RESERVE_PORT_ID_CPU_OAM    71

#define SYS_RESERVE_PORT_MAX          75
#define SYS_MAX_LOCAL_PORT            128

#define SYS_RESERVE_PORT_INTERNAL_PORT_NUM 8
/* 76 */
#define SYS_INTERNAL_PORT_START          (SYS_RESERVE_PORT_MAX + 1)
/* 128-76 = 52 */
#define SYS_INTERNAL_PORT_NUM            ((SYS_MAX_LOCAL_PORT) - (SYS_INTERNAL_PORT_START))

#define SYS_IS_LOOP_PORT(port) ((SYS_RESERVE_PORT_ID_ILOOP == port) || (SYS_RESERVE_PORT_IP_TUNNEL == port))

/**** mac ****/
#define SYS_MAX_GMAC_PORT_NUM   48
#define SYS_MAX_SGMAC_PORT_NUM  12


/**** channel ****/
#define SYS_MAX_CHANNEL_NUM     64


#define SYS_INTLK_CHANNEL_ID  SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_INTLK)
#define SYS_ILOOP_CHANNEL_ID  SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_ILOOP)
#define SYS_CPU_CHANNEL_ID    SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_CPU)
#define SYS_DMA_CHANNEL_ID    SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_DMA)
#define SYS_OAM_CHANNEL_ID    SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_OAM)
#define SYS_ELOOP_CHANNEL_ID  SYS_GET_CHANNEL_ID(lchip, SYS_RESERVE_PORT_ID_ELOOP)


#define MAX_SYS_L2PDU_BASED_L2HDR_PTL_ENTRY 16
#define MAX_SYS_L2PDU_BASED_MACDA_LOW24_ENTRY 10
#define MAX_SYS_L2PDU_BASED_MACDA_ENTRY 4
#define MAX_SYS_L2PDU_BASED_L3TYPE_ENTRY 15
#define MAX_SYS_L2PDU_BASED_BPDU_ENTRY 1
#define MAX_SYS_L3PDU_BASED_L3HDR_PROTO 8
#define MAX_SYS_L3PDU_BASED_PORT 8

#define SYS_GLOBAL_CHIPID_CHECK(gchip)      \
    if ((gchip == 0x1F) || (gchip > SYS_GB_MAX_GCHIP_CHIP_ID))          \
    {                                       \
        return CTC_E_INVALID_CHIP_ID; \
    }


#define SYS_MAX_L2PDU_PER_PORT_ACTION_INDEX  31
#define SYS_MAX_L3PDU_PER_L3IF_ACTION_INDEX  31

#define SYS_GREATBELT_GPORT_TO_GPORT14(gport) \
    do \
    { \
        gport = (((gport >> 8) << 9)) | (gport& 0xFF); \
    } while (0)

#define SYS_GREATBELT_GPORT14_TO_GPORT(gport) \
    do \
    { \
        gport = (((gport >> 9) << 8)) | (gport& 0xFF); \
    } while (0)

#define SYS_GREATBELT_GPORT13_TO_GPORT14(gport) ((((gport >> 8) << 9)) | (gport & 0xFF) | (((gport >> 15) & 0x1) << 8))

#define SYS_GREATBELT_GPORT14_TO_GPORT13(gport) ((((gport >> 9) << 8)) | (gport & 0xFF) | (((gport >> 8) & 0x1) << 15))




#define SYS_GREATBELT_BUILD_DESTMAP(is_mcast, chip_id, destid) \
    (((is_mcast & 0x1) << 21) | ((chip_id & 0x1F) << 16) | (destid & 0xFFFF))

#define SYS_GREATBELT_DESTMAP_TO_GPORT(destmap) \
    (((((destmap >> 16) & 0x1F) << 8)) | (destmap & 0xFF)); \


extern char*
sys_greatbelt_output_mac(mac_addr_t in_mac);

extern uint8
sys_greatebelt_common_get_mac_id(uint8 lchip, uint16 gport);

extern uint8
sys_greatebelt_common_get_channel_id(uint8 lchip, uint16 gport);

extern uint8
sys_greatebelt_common_get_lport_with_mac(uint8 lchip, uint8 mac_id);

extern uint8
sys_greatebelt_common_get_lport_with_chan(uint8 lchip, uint8 chan_id);

extern bool
sys_greatbelt_common_check_mac_valid(uint8 lchip, uint8 mac_id);

extern int32
sys_greatbelt_common_init_unknown_mcast_tocpu(uint8 lchip, uint32* iloop_nhid);

#ifdef __cplusplus
}
#endif

#endif

