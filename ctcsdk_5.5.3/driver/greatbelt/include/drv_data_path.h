#ifndef _DRV_HUMBER_DATA_PATH_H
#define _DRV_HUMBER_DATA_PATH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sal.h"
 /*-#include "dal.h"*/
#include "greatbelt/include/drv_common.h"

#define HSS_MACRO_NUM  4
#define HSS_SERDES_LANE_NUM 32
#define SGMAC_NUM 12
#define QSGMII_NUM 12
#define QUADMAC_NUM 12
#define GMAC_NUM 48
#define DRV_MAX_CHIP_NUM 30
#define HSS_INTERNEL_LANE_TX_REG0_MASK  0xffffffdc
#define HSS_INTERNEL_LANE_RX_REG0_MASK  0xffffff00

#define MAX_TX_POINTER_IDX 1792
#define MAC_NUM 60
#define CHAN_NUM 64
#define INVALID_CHAN_ID 0xFF
#define INVALID_LOCAL_PHY_PORT 0xFF
#define INTERLAKEN_NETTX_PORT_ID 60
#define ELOOP_NETTX_PORT_ID 61


#define DRV_DATA_PATH_DROP_LPORT    66

/*Notice: internel port can not change simply, drv_greatbelt_datapath_cfg_calendar need using lport
   sequence to init bufretrive resource */
#define DRV_DATA_PATH_INTLK_LPORT   60
#define DRV_DATA_PATH_ILOOP_LPORT   61
#define DRV_DATA_PATH_CPU_LPORT     62
#define DRV_DATA_PATH_DMA_LPORT     63
#define DRV_DATA_PATH_OAM_LPORT     64
#define DRV_DATA_PATH_ELOOP_LPORT   65

#define DRV_DATA_PATH_DROP_DEF_CHAN    57
#define DRV_DATA_PATH_INTLK_DEF_CHAN   58
#define DRV_DATA_PATH_OAM_DEF_CHAN     59

#define DRV_DATA_PATH_DMA_DEF_CHAN     60
#define DRV_DATA_PATH_CPU_DEF_CHAN     61
#define DRV_DATA_PATH_ILOOP_DEF_CHAN   62
#define DRV_DATA_PATH_ELOOP_DEF_CHAN   63


#define DRV_DATAPATH_BIT_COMBINE(w, s, e)  (((w) << 24)|(((e)&0x1ff)<<12)|((s)&0x1ff))


/* extra sgmac base */
#define EXTRA_SGMAC_BASE 8
#define DATAPATH_WRITE_CHIP(CHIP, ADDRESS, VALUE)    \
    if (drv_greatbelt_chip_write((CHIP), (ADDRESS), (VALUE))) \
    { \
            return DRV_E_DATAPATH_WRITE_CHIP_FAIL; \
    }
#define DATAPATH_READ_CHIP(CHIP, ADDRESS, VALUE)     \
    if (drv_greatbelt_chip_read((CHIP), (ADDRESS), (VALUE))) \
    { \
        return DRV_E_DATAPATH_READ_CHIP_FAIL; \
    }

#define DATAPATH_WRITE_HSS12G(CHIP, HSSID, ADDRESS, VALUE)    \
    if (drv_greatbelt_chip_write_hss12g((CHIP), (HSSID), (ADDRESS), (VALUE))) \
    { \
            return DRV_E_DATAPATH_WRITE_CHIP_FAIL; \
    }

#define DATAPATH_READ_HSS12G(CHIP, HSSID, ADDRESS, VALUE)     \
    if (drv_greatbelt_chip_read_hss12g((CHIP), (HSSID), (ADDRESS), (VALUE))) \
    { \
        return DRV_E_DATAPATH_READ_CHIP_FAIL; \
    }

#define DRV_DATAPATH_DBG_OUT(fmt, args...)            \
{ \
    if (g_datapath_debug_on)  \
    {   \
        sal_printf(fmt, ##args);                        \
    }   \
}

enum drv_chip_item_type_e
{
    DRV_CHIP_ITEM_TYPE_CHIP_ID = 1
};
typedef enum drv_chip_item_type_e drv_chip_item_type_t;

enum drv_serdes_item_type_e
{
    DRV_SERDES_ITEM_NTSG0 = 0,
    DRV_SERDES_ITEM_NTSG1,
    DRV_SERDES_ITEM_NTSG2,
    DRV_SERDES_ITEM_NTSG3,
    DRV_SERDES_ITEM_NTSG4,
    DRV_SERDES_ITEM_NTSG5,
    DRV_SERDES_ITEM_NTSG6,
    DRV_SERDES_ITEM_NTSG7,
    DRV_SERDES_ITEM_NTSG8,
    DRV_SERDES_ITEM_NTSG9,
    DRV_SERDES_ITEM_NTSG10,
    DRV_SERDES_ITEM_NTSG11,
    DRV_SERDES_ITEM_NTSG12,
    DRV_SERDES_ITEM_NTSG13,
    DRV_SERDES_ITEM_NTSG14,
    DRV_SERDES_ITEM_NTSG15,
    DRV_SERDES_ITEM_NTSG16,
    DRV_SERDES_ITEM_NTSG17,
    DRV_SERDES_ITEM_NTSG18,
    DRV_SERDES_ITEM_NTSG19,
    DRV_SERDES_ITEM_NTSG20,
    DRV_SERDES_ITEM_NTSG21,
    DRV_SERDES_ITEM_NTSG22,
    DRV_SERDES_ITEM_NTSG23,
    DRV_SERDES_ITEM_NTSG24,
    DRV_SERDES_ITEM_NTSG25,
    DRV_SERDES_ITEM_NTSG26,
    DRV_SERDES_ITEM_NTSG27,
    DRV_SERDES_ITEM_NTSG28,
    DRV_SERDES_ITEM_NTSG29,
    DRV_SERDES_ITEM_NTSG30,
    DRV_SERDES_ITEM_NTSG31
};
typedef enum drv_serdes_item_type_e drv_serdes_item_type_t;

enum drv_serdes_speed_e
{
    DRV_SERDES_SPPED_1DOT25G = 0,
    DRV_SERDES_SPPED_3DOT125G,
    DRV_SERDES_SPPED_5G,
    DRV_SERDES_SPPED_6DOT25G,
    DRV_SERDES_SPPED_10DOT3125G,
    DRV_SERDES_SPPED_5G15625G
};
typedef enum drv_serdes_speed_e drv_serdes_speed_t;


enum drv_pll_item_type_e
{
    DRV_PLL_ITEM_CORE = 0,
    DRV_PLL_ITEM_TANK,
    DRV_PLL_ITEM_HSS0,
    DRV_PLL_ITEM_HSS1,
    DRV_PLL_ITEM_HSS2,
    DRV_PLL_ITEM_HSS3,
    DRV_PLL_ITEM_MAX
};
typedef enum drv_pll_item_type_e drv_pll_item_type_t;

enum drv_sync_item_type_e
{
    DRV_SYNCE_ITEM_ITEM0 = 0,
    DRV_SYNCE_ITEM_ITEM1,
    DRV_SYNCE_ITEM_ITEM2,
    DRV_SYNCE_ITEM_ITEM3,

    DRV_SYNCE_ITEM_MAX
};
typedef enum drv_sync_item_type_e drv_sync_item_type_t;

enum drv_calendar_item_type_e
{
    DRV_CALENDAR_PTR_ITEM_CTL = 0,      /* main calendar config*/
    DRV_CALENDAR_PTR_ITEM_BAK,           /* use for dynamic switch */
    DRV_CALENDAR_PTR_ITEM_SEL            /* sel using CTL or BAK */
};
typedef enum drv_calendar_item_type_e drv_calendar_item_type_t;

enum drv_net_tx_calendar_10g_item_type_e
{
    DRV_NET_TX_CALENDAR_10G_ENTRY_NUM = 1
};
typedef enum drv_net_tx_calendar_10g_item_type_e drv_net_tx_calendar_10g_item_type_t;

enum drv_misc_item_type_e
{
    DRV_MISC_ITEM_CPU_MAC = 0,
    DRV_MISC_ITEM_PTP_ENGINE,
    DRV_MISC_ITEM_LED_DRIVER,

    DRV_MISC_ITEM_MAX
};
typedef enum drv_misc_item_type_e drv_misc_item_type_t;

enum drv_mac_cap_item_type_e
{
    DRV_MAC_CAP_MACID_ITEM = 0,
    DRV_MAC_CAP_CHANID_ITEM,
    DRV_MAC_CAP_USED_ITEM,

    DRV_MAC_CAP_MACID_ITEM_MAX
};
typedef enum drv_mac_cap_item_type_e drv_mac_cap_item_type_e;

/*port type*/
enum drv_port_type_e
{
    DRV_PORT_TYPE_NONE = 0,    /* none */
    DRV_PORT_TYPE_1G,          /* 1G */
    DRV_PORT_TYPE_SG,          /* 10G*/
    DRV_PORT_TYPE_DMA,          /* dma */
    DRV_PORT_TYPE_CPU,          /* cpu */
     /*DRV_PORT_TYPE_INTERNAL,      // Internal */
    DRV_PORT_TYPE_MAX
};
typedef enum drv_port_type_e drv_port_type_t;

/* channel info: mac id(port id), internal port and port type  */
struct drv_datapath_chan_info_s
{
    uint8 valid;                /* used or not */
    uint8 port_id;              /* used for chan maping netrx/nettx */
    uint8 local_phy_port;       /* used for get attribute in chip */
    uint8 rsv[1];
    drv_port_type_t port_type;  /* drv_port_type_t */
};
typedef struct drv_datapath_chan_info_s drv_datapath_chan_info_t;


/* local phy port info */
struct drv_datapath_port_capability_s
{
    uint8 valid;                   /* used or not */
    uint8 chan_id;                 /* used for chan maping netrx/nettx */
    uint8 mac_id;                  /* used for get attribute in chip */
    uint8 serdes_id;               /* 0-31 */
    uint8 pcs_mode;                /* drv_hss_serdes_mode_t */
    uint8 speed_mode;              /* drv_serdes_speed_t */
    uint8 hss_id;                  /* 0-3 */
    uint8 rsv;
    drv_port_type_t port_type;
    uint8 first_led_mode;          /* when led_mode tx activ, when port link down, need to set force led off mode */
    uint8 sec_led_mode;
    uint8 code_err_count;          /* store code err count*/
};
typedef struct drv_datapath_port_capability_s drv_datapath_port_capability_t;

/* serdes info */
struct drv_datapath_serdes_capability_s
{
    uint8 pcs_mode;        /* refer to drv_hss_serdes_mode_t */
    uint8 speed;
    uint8 mac_id;
    uint8 chan_id;
    uint8 hss_id;         /* 0-3 */
    uint8 lane_idx;       /* this is only used for cofig hss12g, for chip reg can not use this value */
    uint8 dynamic;        /* whether support dynamic switch */
    uint8 rsv;
};
typedef struct drv_datapath_serdes_capability_s drv_datapath_serdes_capability_t;


enum drv_cpumac_type_e
{
    DRV_CPU_MAC_TYPE_NA = 0,
    DRV_CPU_MAC_TYPE_10M,
    DRV_CPU_MAC_TYPE_100M,
    DRV_CPU_MAC_TYPE_GE,

    DRV_CPU_MAC_TYPE_MAX
};
typedef enum drv_cpumac_type_e drv_cpumac_type_t;

struct drv_chip_item_s
{
    uint8 seq;
    uint8 rsv[3];
};
typedef struct drv_chip_item_s drv_chip_item_t;

typedef struct drv_para_pair_s
{
    const char* para_name;                     /* the parameter name */
    int32 (* fun_ptr)(const char* line, void* argus); /* get the value from line */
    void* argus;                               /* parameter for fun_ptr */
} drv_para_pair_t;

enum drv_hss_serdes_mode_e
{
    DRV_SERDES_SGMII_MODE,
    DRV_SERDES_QSGMII_MODE,
    DRV_SERDES_XAUI_MODE,
    DRV_SERDES_XGSGMII_MODE,
    DRV_SERDES_XFI_MODE,
    DRV_SERDES_INTLK_MODE,
    DRV_SERDES_NONE_MODE,
     /*DRV_SERDES_MIX_MODE,           // means one macro have more than one mode, or support dynamic switch */
    DRV_SERDES_MIX_MODE_WITH_QSGMII, /* SGMII(XSGMII) with QSGMII */
    DRV_SERDES_MIX_MODE_WITH_XFI,    /* SGMII(XSGMII) with XFI */
    DRV_SERDES_NA_SGMII_MODE, /*internal convert NA to SGMII*/
    DRV_SERDES_2DOT5_MODE, /*2.5G serdes*/
    DRV_HSS_MAX_LANE_MODE
};
typedef enum drv_hss_serdes_mode_e drv_hss_serdes_mode_t;

struct drv_datapath_serdes_type_s
{
    drv_hss_serdes_mode_t hss_lane_mode;
    uint8 mac_id;      /* qsgmii mode means quadmac id, sgmii mode means gmac id, xfi/xaui mode means sgmac id */
    uint8 rsv[3];
};
typedef struct drv_datapath_serdes_type_s drv_datapath_serdes_type_t;

enum drv_chip_mac_type_e
{
    DRV_CHIP_GMAC_TYPE,
    DRV_CHIP_SGMAC_TYPE,
    DRV_CHIP_QUADMAC_TYPE
};
typedef enum drv_chip_mac_type_e drv_chip_mac_type_t;

enum drv_chip_mac_info_type_e
{
    DRV_CHIP_MAC_PCS_INFO,
    DRV_CHIP_MAC_SERDES_INFO,
    DRV_CHIP_MAC_CHANNEL_INFO,
    DRV_CHIP_MAC_VALID,                         /* for sgmac and gmac*/
    DRV_CHIP_MAC_QUADMAC_VALID,        /* for quadmac */

    DRV_CHIP_MAC_ALL_INFO
};
typedef enum drv_chip_mac_info_type_e drv_chip_mac_info_type_t;

enum drv_chip_dynamic_switch_type_e
{
    DRV_CHIP_SWITCH_10G1G,
    DRV_CHIP_SWITCH_5G1G,

    DRV_CHIP_SWITCH_MAX_TYPE
};
typedef enum drv_chip_dynamic_switch_type_e drv_chip_dynamic_switch_type_t;

enum drv_chip_port_info_type_e
{
    DRV_CHIP_PORT_CHAN_INFO,

    DRV_CHIP_PORT_ALL_INFO
};
typedef enum drv_chip_port_info_type_e drv_chip_port_info_type_t;

enum drv_chip_chan_info_type_e
{
    DRV_CHIP_CHAN_VALID_INFO,
    DRV_CHIP_CHAN_NETTX_PORT_INFO,
    DRV_CHIP_CHAN_LPORT_INFO,
    DRV_CHIP_CHAN_PORT_TYPE_INFO,

    DRV_CHIP_CHAN_ALL_INFO
};
typedef enum drv_chip_chan_info_type_e drv_chip_chan_info_type_t;

enum drv_chip_serdes_info_type_e
{
    DRV_CHIP_SERDES_MODE_INFO,
    DRV_CHIP_SERDES_SPEED_INFO,
    DRV_CHIP_SERDES_MACID_INFO,
    DRV_CHIP_SERDES_CHANNEL_INFO,
    DRV_CHIP_SERDES_HSSID_INFO,
    DRV_CHIP_SERDES_LANE_INFO,
    DRV_CHIP_SERDES_SWITCH_INFO,

    DRV_CHIP_SERDES_ALL_INFO
};
typedef enum drv_chip_serdes_info_type_e drv_chip_serdes_info_type_t;

enum drv_chip_hss_info_type_e
{
    DRV_CHIP_HSS_MODE_INFO,
    DRV_CHIP_HSS_SGMAC4_INFO,
    DRV_CHIP_HSS_SGMAC5_INFO,
    DRV_CHIP_HSS_SGMAC6_INFO,
    DRV_CHIP_HSS_SGMAC7_INFO,
    DRV_CHIP_HSS_SGMAC8_INFO,
    DRV_CHIP_HSS_SGMAC9_INFO,
    DRV_CHIP_HSS_SGMAC10_INFO,
    DRV_CHIP_HSS_SGMAC11_INFO,
    DRV_CHIP_HSS_QSGMII0_INFO,
    DRV_CHIP_HSS_QSGMII1_INFO,
    DRV_CHIP_HSS_QSGMII2_INFO,
    DRV_CHIP_HSS_QSGMII3_INFO,
    DRV_CHIP_HSS_QSGMII6_INFO,
    DRV_CHIP_HSS_QSGMII7_INFO,
    DRV_CHIP_HSS_QSGMII8_INFO,
    DRV_CHIP_HSS_QSGMII9_INFO,

};
typedef enum drv_chip_hss_info_type_e drv_chip_hss_info_type_t;

struct drv_datapath_core_pll_s
{
    uint32 is_used;
    uint32 ref_clk;
    uint32 output_a;            /* for clockcore */
    uint32 output_b;            /* for Tcam */
    uint32 core_pll_cfg1;
    uint32 core_pll_cfg2;
    uint32 core_pll_cfg3;
    uint32 core_pll_cfg4;
    uint32 core_pll_cfg5;
};
typedef struct drv_datapath_core_pll_s drv_datapath_core_pll_t;

struct drv_datapath_tank_pll_s
{
    uint32 is_used;
    uint32 ref_clk;
    uint32 output_a;
    uint32 output_b;
    uint32 tank_pll_cfg1;
    uint32 tank_pll_cfg2;
};
typedef struct drv_datapath_tank_pll_s drv_datapath_tank_pll_t;

struct drv_datapath_hss_pll_s
{
    uint8 is_used;
    uint8 aux_sel;
    uint8 rsv[2];
    uint32 hss_pll_cfg1;
    uint32 hss_pll_cfg2;
};
typedef struct drv_datapath_hss_pll_s drv_datapath_hss_pll_t;

/* datapath pll config info */
struct drv_datapath_pll_info_s
{
    drv_datapath_core_pll_t core_pll;
    drv_datapath_tank_pll_t tank_pll;
    drv_datapath_hss_pll_t  hss_pll[HSS_MACRO_NUM];
};
typedef struct drv_datapath_pll_info_s drv_datapath_pll_info_t;

struct drv_datapath_sgmac_info_s
{
    uint8 valid;
    uint8 pcs_mode;                 /* XFI or Xaui */
    uint8 chan_id;
    uint8 serdes_id;                   /* serdes index used by sgmac*/
};
typedef struct drv_datapath_sgmac_info_s drv_datapath_sgmac_info_t;

struct drv_datapath_quadmac_info_s
{
    uint8 valid;
    uint8 pcs_mode;                    /* 0-Sgmii, 1-Qsgmii */
    uint8 serdes_id;
    uint8 chan_id;                        /* no use for qusgmii */
};
typedef struct drv_datapath_quadmac_info_s drv_datapath_quadmac_info_t;

struct drv_datapath_gmac_info_s
{
    uint8 valid;
    uint8 pcs_mode;
    uint8 serdes_id;
    uint8 chan_id;
};
typedef struct drv_datapath_gmac_info_s drv_datapath_gmac_info_t;

struct drv_datapath_serdes_info_s
{
    uint8 mode;        /* refer to drv_hss_serdes_mode_t */
    uint8 speed;
    uint8 mac_id;
    uint8 chan_id;

    uint8 hss_id;
    uint8 lane_idx;       /* this is only used for cofig hss12g, for chip reg can not use this value */
    uint8 dynamic;       /* whether support dynamic switch */
    uint8 rsv;
};
typedef struct drv_datapath_serdes_info_s drv_datapath_serdes_info_t;
struct drv_datapath_nettxbufcfg_info_s
{
    uint16 gmac_step;
    uint16 sgmac_step;

    uint16 intlk_step;
    uint16 dxaui_step;

    uint16 eloop_step;
    uint8  rsv[2];

    uint32 cur_ptr;
};
typedef struct drv_datapath_nettxbufcfg_info_s drv_datapath_nettxbufcfg_info_t;

struct drv_datapath_sync_s
{
    uint32 is_used;
    uint32 sync_cfg1;
    uint32 sync_cfg2;
};
typedef struct drv_datapath_sync_s drv_datapath_sync_t;

struct drv_datapath_cal_info_s
{
    uint32 net_tx_cal_entry[160];
    uint8  cal_walk_end;
    uint8  rsv[3];
};
typedef struct drv_datapath_cal_info_s drv_datapath_cal_info_t;

struct drv_datapath_misc_info_s
{
    drv_cpumac_type_t cpumac_type;
    uint32 ptp_used;
    uint32 mac_led_used;
};
typedef struct drv_datapath_misc_info_s drv_datapath_misc_info_t;

/* datapath hss config info */
struct drv_datapath_hss_info_s
{
    uint8 hss_mode[HSS_MACRO_NUM];                /* 0-Sgmii, 1-Qsgmii, 2-xfi, 3-xaui */
    uint8 hss_used_sgmac10;                            /* sgmac10 select hss0/hss1/hss2 */
    uint8 hss_used_sgmac11;                            /* sgmac11 select hss0/hss1/hss2 */
    uint8 hss_used_sgmac6;                             /* sgmac6 select hss3/hss2 */
    uint8 hss_used_sgmac7;                             /* sgmac7 select hss3/hss2 */
    uint8 hss_used_sgmac8;                             /* sgmac8 select hss0/hss2 */
    uint8 hss_used_sgmac9;                             /* sgmac9 select hss0/hss2 */
    uint8 sgmac4_is_xaui;                                     /* hss4 is xaui or not */
    uint8 sgmac5_is_xaui;                                     /* hss5 is xaui or not */
    uint8 qsgmii9_sel_hss3;                              /* qsgmii9 select hss3LanB or Hss1lanF */
    uint8 qsgmii8_sel_hss3;                              /* qsgmii8 select hss3LanA or Hss1LanB */
    uint8 qsgmii7_sel_hss2;                              /* qsgmii7 select hss2LanD or Hss0LanF */
    uint8 qsgmii6_sel_hss2;                              /* qsgmii6 select hss2LanB or Hss0LanB */
    uint8 qsgmii3_sel_hss3;                              /* qsgmii3 select hss3LanF or Hss1LanE */
    uint8 qsgmii2_sel_hss3;                              /* qsgmii2 select hss3LanE or Hss1LanA */
    uint8 qsgmii1_sel_hss2;                              /* qsgmii1 select hss2LanH or Hss0LanE */
    uint8 qsgmii0_sel_hss2;                              /* qsgmii0 select hss2LanF or Hss0LanA */

};
typedef struct drv_datapath_hss_info_s drv_datapath_hss_info_t;
enum drv_nettxbufcfg_item_type_e
{
    DRV_NETTXBUFCFG_ITEM_1G,
    DRV_NETTXBUFCFG_ITEM_SG,
    DRV_NETTXBUFCFG_ITEM_INTLK,
    DRV_NETTXBUFCFG_ITEM_DXAUI,
    DRV_NETTXBUFCFG_ITEM_ELOOP
};
typedef enum drv_nettxbufcfg_item_type_e drv_nettxbufcfg_item_type_t;


enum drv_datapath_inter_chan_type_e
{
    DRV_INTLK_CHAN_TYPE,
    DRV_ILOOP_CHAN_TYPE,
    DRV_CPU_CHAN_TYPE,
    DRV_ELOOP_CHAN_TYPE,
    DRV_OAM_CHAN_TYPE,
    DRV_DMA_CHAN_TYPE,
    DRV_INTER_CHAN_TYPE
};
typedef enum drv_datapath_inter_chan_type_e drv_datapath_inter_chan_type_t;

enum drv_datapath_clock_type_e
{
    DRV_CORE_CLOCK_TYPE,
    DRV_CORE_REF_CLOCK_TYPE,
    DRV_TANK_CLOCK_TYPE,
    DRV_TANK_REF_CLOCK_TYPE

};
typedef enum drv_datapath_clock_type_e drv_datapath_clock_type_t;


struct drv_datapath_master_s
{
    drv_datapath_pll_info_t pll_cfg;
    drv_datapath_hss_info_t hss_cfg;
    drv_datapath_sgmac_info_t sgmac_cfg[SGMAC_NUM];
    drv_datapath_quadmac_info_t quadmac_cfg[QUADMAC_NUM];   /*qsgmii mode using*/
    drv_datapath_gmac_info_t gmac_cfg[GMAC_NUM];                   /*sgmii mode using */
    drv_datapath_serdes_info_t serdes_cfg[HSS_SERDES_LANE_NUM];
    drv_datapath_sync_t sync_cfg[4];
    drv_datapath_cal_info_t cal_infor[2];
    drv_datapath_misc_info_t misc_info;
    drv_datapath_nettxbufcfg_info_t nettxbuf_cfg;

    drv_datapath_port_capability_t port_capability[MAX_LOCAL_PORT_NUM];
    uint8 chan_used[CHAN_NUM];

    uint8  calendar_sel;
    uint8  is_mix;
    uint8  ignore_mode; /* 1: ignore linkIntFault, remoteFault and localFault, only valid for XFI */
    uint8  net_tx_cal_10g_value;
    uint8  init_flag;
    uint8  rsv[3];

};
typedef struct drv_datapath_master_s drv_datapath_master_t;

#define DRV_HSS12G_PORT_EN_REG1  6
#define DRV_HSS12G_PORT_EN_REG2  7
#define DRV_HSS12G_PORT_RESET_REG1  8
#define DRV_HSS12G_PORT_RESET_REG2  9
#define DRV_HSS12G_PLL_COMMON_HIGH_ADDR  16
#define DRV_HSS12G_ADDRESS_CONVERT(addr_h, addr_l)   (((addr_h) << 6) | (addr_l))

typedef int32 (* chip_reset_cb)(uint8 type, uint32 flag);

int32
drv_greatbelt_parse_datapath_file(char* datapath_config_file);
int32
drv_greatbelt_init_pll_hss(void);
int32
drv_greatbelt_init_total(void);
int32
drv_greatbelt_get_datapath(uint8 lchip, drv_datapath_master_t** p_datapath);
uint32
drv_greatbelt_get_clock(uint8 chip_id, drv_datapath_clock_type_t clock_type);
int32
drv_greatbelt_datapath_sim_init(void);
int32
drv_greatbelt_get_serdes_info(uint8 chip_id, uint8 serdes_idx, drv_chip_serdes_info_type_t info_type, void* p_serdes_info);
int32
drv_greatbelt_set_serdes_info(uint8 chip_id, uint8 serdes_idx, drv_chip_serdes_info_type_t info_type, void* p_info);
bool
drv_greatbelt_get_intlk_support(uint8 lchip);
uint8
drv_greatbelt_get_port_capability(uint8 lchip, uint8 lport, drv_datapath_port_capability_t *port_cap);
int32
drv_greatbelt_set_port_capability(uint8 lchip, uint8 lport, drv_datapath_port_capability_t port_cap);
int32
drv_greatbelt_set_hss_info(uint8 chip_id, uint8 hss_id, drv_chip_hss_info_type_t info_type, void* p_hss_info);
int32
drv_greatbelt_datapath_force_signal_detect(uint8 lchip, bool enable);
uint8
drv_greatbelt_get_ignore_mode(uint8 lchip);
int32
drv_greatbelt_get_port_with_serdes(uint8 lchip, uint16 serdes_id, uint16* p_lport);
uint8
drv_greatbelt_get_port_attr(uint8 lchip, uint8 lport, drv_datapath_port_capability_t** port_cap);
#ifdef __cplusplus
}
#endif

#endif
