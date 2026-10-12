#ifndef _CTC_GREATBELT_DKIT_H_
#define _CTC_GREATBELT_DKIT_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
#include "ctc_greatbelt_dkit_discard.h"
#include "ctc_greatbelt_dkit_path.h"


#define CTC_DKIT_ONE_SLICE_CHANNEL_NUM 64
#define CTC_DKIT_GCHIP_MASK                  0x1F        /**< Gchip id mask(unit: bit) */
#define CTC_DKIT_LOCAL_PORT_LENGTH           8           /**< Local port id length(unit: bit) */
#define CTC_DKIT_LOCAL_PORT_MASK             0xFF        /**< Local port mask */
#define CTC_DKIT_GCHIP_LENGTH                5           /**< Gchip id length(unit: bit) */
#define CTC_DKIT_GCHIP_MASK                  0x1F        /**< Gchip id mask(unit: bit) */
#define CTC_DKIT_EXT_PORT_LENGTH             1           /**< Local port id ext length(unit: bit) */
#define CTC_DKIT_EXT_PORT_MASK               7           /**< Local port ext bit mask */

#define CTC_DKIT_NETWORK_MIN_CHANID               0
#define CTC_DKIT_NETWORK_MAX_CHANID              55
#define CTC_DKIT_INTERLAKEN_CHANID               56
#define CTC_DKIT_I_LOOPBACK_CHANID               57
#define CTC_DKIT_CPU_CHANID                      58
#define CTC_DKIT_DMA_CHANID                      59
#define CTC_DKIT_OAM_CHANID                      60
#define CTC_DKIT_E_LOOPBACK_CHANID               61

#define CTC_DKIT_LINKAGG_CHIPID              0x1F        /**< Linkagg port's global chip ID */

#define CTC_DKIT_MAP_GPORT_TO_GCHIP(gport)   (((gport) >> CTC_DKIT_LOCAL_PORT_LENGTH) & CTC_DKIT_GCHIP_MASK)
#define CTC_DKIT_MAP_GPORT_TO_LPORT(gport)   ((gport) & CTC_DKIT_LOCAL_PORT_MASK)
#define CTC_MAP_LPORT_TO_GPORT(gchip, lport)  (((((lport) >> CTC_DKIT_LOCAL_PORT_LENGTH) & CTC_DKIT_EXT_PORT_MASK) \
                                              << (CTC_DKIT_LOCAL_PORT_LENGTH + CTC_DKIT_GCHIP_LENGTH))           \
                                              | (((gchip) & CTC_DKIT_GCHIP_MASK) << CTC_DKIT_LOCAL_PORT_LENGTH)    \
                                              | ((lport) & CTC_DKIT_LOCAL_PORT_MASK))
#define CTC_DKIT_DRV_GPORT_IS_LINKAGG_PORT(gport)         (CTC_DKIT_MAP_GPORT_TO_GCHIP(gport) == CTC_DKIT_LINKAGG_CHIPID)
#define CTC_DKIT_GET_GCHIP(lchip, gchip) \
        do { \
                 uint32 cmd_tmp = 0; \
                 uint8 i_tmp = 0; \
                 uint32 gport_tmp = 0; \
                 for (i_tmp = 0; i_tmp< 127; i_tmp++) \
                 { \
                     cmd_tmp = DRV_IOR(DsPhyPortExt_t, DsPhyPortExt_GlobalSrcPort_f); \
                     DRV_IOCTL(lchip, i_tmp, cmd_tmp, &gport_tmp); \
                     if (!CTC_DKIT_DRV_GPORT_IS_LINKAGG_PORT(gport_tmp)) {break;} \
                 } \
                 gchip =  CTC_DKIT_MAP_GPORT_TO_GCHIP(gport_tmp); \
    } while (0)


struct ctc_dkit_serdes_wr_s
{
    uint32 type;  /*ctc_dkit_serdes_mode_t*/

    uint16 serdes_id ;
    uint16 data;

    uint32 addr_offset;

    uint8 lchip;
    uint8 resv[8];
};
typedef struct ctc_dkit_serdes_wr_s ctc_dkit_serdes_wr_t;


struct ctc_dkit_master_s
{
    ctc_dkit_discard_stats_t discard_stats;
    ctc_dkit_discard_history_t discard_history;
    ctc_dkit_path_stats_info_t path_stats;
    void* discard_enable_bitmap;
};
typedef struct ctc_dkit_master_s ctc_dkit_master_t;

enum ctc_dkit_device_id_type_e
{
    CTC_DKIT_DEVICE_ID_GB_5160,
    CTC_DKIT_DEVICE_ID_GB_5162,
    CTC_DKIT_DEVICE_ID_GB_5163,
    CTC_DKIT_DEVICE_ID_RM_5120,
    CTC_DKIT_DEVICE_ID_RT_3162,
    CTC_DKIT_DEVICE_ID_RT_3163,
    CTC_DKIT_DEVICE_ID_INVALID
};
typedef enum ctc_dkit_device_id_type_e ctc_dkit_device_id_type_t;

#ifdef __cplusplus
}
#endif

#endif



