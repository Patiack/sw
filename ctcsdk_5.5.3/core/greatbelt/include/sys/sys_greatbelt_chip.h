/**
 @file sys_greatbelt_chip.h

 @date 2009-10-19

 @version v2.0

 The file contains all chip related APIs of sys layer
*/

#ifndef _SYS_GREATBELT_CHIP_H
#define _SYS_GREATBELT_CHIP_H
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
#include "ctc_chip.h"
#include "ctc_debug.h"
#include "sys_greatbelt_chip_global.h"
#include "sys_greatbelt_common.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/
#define SYS_CHIP_HSS12G_MACRO_NUM   4
#define STS_CHIP_HSS12G_LINK_NUM   8
#define SYS_DRV_LOCAL_PORT_LENGTH           9
#define SYS_DRV_LOCAL_PORT_MASK           0x1FF
#define SYS_DRV_GCHIP_MASK                  0x1F

#define SYS_CHIP_CPU_MAC_ETHER_TYPE 0x5a5a
#define SYS_CHIP_SWITH_THREADHOLD 20

#define SYS_MAP_GPORT_TO_LPORT(gport, lchip, lport) \
    { \
        uint8 gchip; \
        gchip = CTC_MAP_GPORT_TO_GCHIP(gport); \
        SYS_GLOBAL_CHIPID_CHECK(gchip); \
        lport = (gport & CTC_LOCAL_PORT_MASK); \
        if (lport >= SYS_GB_MAX_PORT_NUM_PER_CHIP) \
        {return CTC_E_INVALID_LOCAL_PORT; } \
        if (FALSE == sys_greatbelt_chip_is_local(lchip, gchip)) \
        {return CTC_E_CHIP_IS_REMOTE; } \
    }

#define SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip) \
    do{\
       SYS_GLOBAL_CHIPID_CHECK(gchip);\
        if(!g_ctcs_api_en)\
        {\
            if(CTC_E_NONE != sys_greatbelt_get_local_chip_id(gchip, &lchip))\
            {\
                return CTC_E_CHIP_IS_REMOTE;\
            }\
        }\
    }while(0);

#define SYS_MAP_GPORT_TO_GCHIP(gport) (((gport) >> CTC_LOCAL_PORT_LENGTH) & CTC_GCHIP_MASK)

/*Notice: the macro is only used to get driver gport, which can be used for software, actually local phy port supported by gb is 8 bits*/
#define SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport) ((CTC_MAP_GPORT_TO_GCHIP(gport) << SYS_DRV_LOCAL_PORT_LENGTH) |CTC_MAP_GPORT_TO_LPORT(gport))
#define SYS_MAP_DRV_PORT_TO_CTC_LPORT(port) (port & SYS_DRV_LOCAL_PORT_MASK)
#define SYS_MAP_CTC_GPORT_TO_GCHIP(gport)     (((gport) >> CTC_LOCAL_PORT_LENGTH) & CTC_GCHIP_MASK)
#define SYS_DRV_GPORT_TO_GCHIP(gport)        (((gport) >> SYS_DRV_LOCAL_PORT_LENGTH) & CTC_GCHIP_MASK)
#define SYS_DRV_IS_LINKAGG_PORT(gport)       ((SYS_DRV_GPORT_TO_GCHIP(gport)&SYS_DRV_GCHIP_MASK) == CTC_LINKAGG_CHIPID)

#define SYS_GCHIP_IS_REMOTE(lchip, gchip) ((0x1F != gchip)&&(!sys_greatbelt_chip_is_local(lchip, gchip)))

#define SYS_LOCAL_CHIPID_CHECK(lchip) \
    do { \
        uint8 local_chip_num = sys_greatbelt_get_local_chip_num(lchip); \
        if (lchip >= local_chip_num){ return CTC_E_INVALID_LOCAL_CHIPID; } \
    } while (0);

#define SYS_CHIP_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(chip, chip, CHIP_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

/*#define SYS_HUMBER_CHIP_CLOCK 625*/
#define CHIP_LOCK \
    if (p_gb_chip_master[lchip]->p_chip_mutex) sal_mutex_lock(p_gb_chip_master[lchip]->p_chip_mutex)

#define CHIP_UNLOCK \
    if (p_gb_chip_master[lchip]->p_chip_mutex) sal_mutex_unlock(p_gb_chip_master[lchip]->p_chip_mutex)

#define FEATURE_SUPPORT_CHECK(lchip, feature) \
{\
    if (CTC_FEATURE_MAX <= feature) \
        return CTC_E_INTR_INVALID_PARAM; \
    if (FALSE == sys_greatbelt_chip_check_feature_support(lchip, feature)) \
        return CTC_E_NOT_SUPPORT;\
}

#define SYS_GB_MAX_LOCAL_CHIP_NUM               30
#define SYS_GB_MAX_PHY_PORT                     60
#define SYS_GB_MAX_VRFID_ID                     255
#define SYS_GB_MAX_LINKAGG_GROUP_NUM            64
#define SYS_GB_MAX_PORT_NUM_PER_CHIP            128
#define SYS_GB_MAX_ECPN                         16
#define SYS_GB_MAX_IP_RPF_IF                    16
#define SYS_GB_MAX_GPORT_ID                     0x1F3F
#define SYS_GB_MIN_CTC_L3IF_ID                  1
#define SYS_GB_MAX_CTC_L3IF_ID                  1022
#define SYS_GB_MAX_APS_GROUP_NUM                1024
#define SYS_GB_MAX_DEFAULT_ECMP_NUM             8
#define SYS_GB_MAX_ACL_PORT_BITMAP_NUM          56
#define SYS_GB_MAX_ACL_PORT_CLASSID             1023
#define SYS_GB_MIN_ACL_PORT_CLASSID             1
#define SYS_GB_MAX_SCL_PORT_CLASSID             61
#define SYS_GB_MIN_SCL_PORT_CLASSID             1
#define SYS_GB_MAX_ACL_VLAN_CLASSID             1023
#define SYS_GB_MIN_ACL_VLAN_CLASSID             1
#define SYS_GB_PKT_CPUMAC_LEN                   20
#define SYS_GB_PKT_HEADER_LEN                   32
#define SYS_GB_LPORT_CPU                        0x10000000
#define SYS_GB_LPORT_OAM                        64
#define SYS_GB_MAX_CPU_MACDA_NUM                4
#define SYS_GB_MAX_LINKAGG_ID                   0x3F
#define SYS_GB_MAX_CPU_REASON_GROUP_NUM         16
#define SYS_GB_MAX_QNUM_PER_GROUP               8
#define SYS_GB_MAX_INTR_GROUP                   6
#define SYS_GB_LINKAGGID_MASK                   0x1F
#define SYS_GB_MAX_IPUC_IP_TUNNEL_IF_NUM        4
#define SYS_GB_DATAPATH_SERDES_NUM              48
#define SYS_GB_IPMC_RP_INTF                     16
#define SYS_GB_MAX_IPMC_RPF_IF                  16
#define SYS_GB_CHIP_FFE_PARAM_NUM               4
#define SYS_GB_MAX_GCHIP_CHIP_ID                0x1E

#define SYS_CHIP_FFE_C0_OFFSET 0x08
#define SYS_CHIP_FFE_C1_OFFSET 0x09
#define SYS_CHIP_FFE_C2_OFFSET 0x0a
#define SYS_CHIP_POLARITY_OFFSET 0x0d

#define SYS_CHIP_PLL_RESET0_OFFSET 0x8
#define SYS_CHIP_PLL_RESET1_OFFSET 0x9

#define SYS_ACL_VLAN_CLASS_ID_CHECK(id) \
    if (((id) < SYS_GB_MIN_ACL_VLAN_CLASSID) || ((id) > SYS_GB_MAX_ACL_VLAN_CLASSID)) \
    { \
        return CTC_E_INVALID_PARAM; \
    }
#define SYS_ACL_PORT_CLASS_ID_CHECK(id) \
    if (((id) < SYS_GB_MIN_ACL_PORT_CLASSID) || ((id) > SYS_GB_MAX_ACL_PORT_CLASSID)) \
    { \
        return CTC_E_INVALID_PARAM; \
    }
#define SYS_SCL_PORT_CLASS_ID_CHECK(id) \
    if (((id) < SYS_GB_MIN_SCL_PORT_CLASSID) || ((id) > SYS_GB_MAX_SCL_PORT_CLASSID)) \
    { \
        return CTC_E_INVALID_PARAM; \
    }

struct sys_chip_master_s
{
    sal_mutex_t* p_chip_mutex;
    uint8   gb_gg_interconnect_en;
    uint8   cut_through_en;
    uint8   g_chip_id;
    uint8   port_mdio_mapping_tbl[SYS_GB_MAX_PHY_PORT];
    uint8   port_phy_mapping_tbl[SYS_GB_MAX_PHY_PORT];
    uint8   mdio_phy_port_mapping_tbl[4][32];
    char    datapath_mode[20];
    uint8   first_ge_opt_reg_used;
    uint8   first_ge_opt_reg;
    uint8   second_ge_opt_reg_used;
    uint8   second_ge_opt_reg;
    mac_addr_t cpu_mac_sa;
    mac_addr_t cpu_mac_da[SYS_GB_MAX_CPU_MACDA_NUM];
    uint8   cut_through_speed;
    uint8   ecc_recover_en;
    uint8   cpu_eth_en;
    uint8   chip_type;
};
typedef struct sys_chip_master_s sys_chip_master_t;

enum sys_chip_mdio_bus_e
{
    SYS_CHIP_MDIO_BUS0,
    SYS_CHIP_MDIO_BUS1,
    SYS_CHIP_MDIO_BUS2,
    SYS_CHIP_MDIO_BUS3,

    SYS_CHIP_MAX_MDIO_BUS
};
typedef enum sys_chip_mdio_bus_e sys_chip_mdio_bus_t;

enum sys_datapath_debug_type_e
{
    SYS_CHIP_DATAPATH_DEBUG_PLL,
    SYS_CHIP_DATAPATH_DEBUG_HSS,
    SYS_CHIP_DATAPATH_DEBUG_SERDES,
    SYS_CHIP_DATAPATH_DEBUG_MAC,
    SYS_CHIP_DATAPATH_DEBUG_SYNCE,
    SYS_CHIP_DATAPATH_DEBUG_CALDE,
    SYS_CHIP_DATAPATH_DEBUG_MISC,
    SYS_CHIP_DATAPATH_DEBUG_CHAN,
    SYS_CHIP_DATAPATH_DEBUG_INTER_PORT,

    SYS_CHIP_HSS_MAX_DEBUG_TYPE
};
typedef enum sys_datapath_debug_type_e sys_datapath_debug_type_t;


enum sys_chip_device_id_type_e
{
    SYS_CHIP_DEVICE_ID_GB_5160,
    SYS_CHIP_DEVICE_ID_GB_5162,
    SYS_CHIP_DEVICE_ID_GB_5163,
    SYS_CHIP_DEVICE_ID_RM_5120,
    SYS_CHIP_DEVICE_ID_RT_3162,
    SYS_CHIP_DEVICE_ID_RT_3163,
    SYS_CHIP_DEVICE_ID_INVALID
};
typedef enum sys_chip_device_id_type_e sys_chip_device_id_type_t;

/*Notice: Cannot change enum, refer to ctc_bpe_intlk_mode_t */
enum sys_chip_intlk_mode_e
{
    SYS_CHIP_INTLK_PKT_MODE,
    SYS_CHIP_INTLK_SEGMENT_MODE,
    SYS_CHIP_INTLK_FABRIC_MODE
};
typedef enum sys_chip_intlk_mode_e sys_chip_intlk_mode_t;

enum sys_chip_type_e
{
    SYS_CHIP_TYPE_GREATBELT,
    SYS_CHIP_TYPE_RAMA,
    SYS_CHIP_TYPE_RIALTO,
    SYS_CHIP_TYPE_MAX
};
typedef enum sys_chip_type_e sys_chip_type_t;
/****************************************************************************
 *
* Function
*
*****************************************************************************/
extern int32
sys_greatbelt_chip_init(uint8 lchip, uint8 lchip_num);

/**
 @brief De-Initialize chip module
*/
extern int32
sys_greatbelt_chip_deinit(uint8 lchip);

extern uint8
sys_greatbelt_get_local_chip_num(uint8 lchip);

extern int32
sys_greatbelt_set_gchip_id(uint8 lchip, uint8 gchip_id);

extern bool
sys_greatbelt_chip_is_local(uint8 lchip, uint8 gchip_id);

extern int32
sys_greatbelt_get_gchip_id(uint8 lchip, uint8* gchip_id);

extern int32
sys_greatbelt_get_local_chip_id(uint8 gchip_id, uint8* lchip_id);

extern int32
sys_greatbelt_datapath_init(uint8 lchip);

extern int32
sys_greatbelt_datapath_deinit(uint8 lchip);

extern int32
sys_greatbelt_chip_ecc_recover_init(uint8 lchip);

extern uint32
sys_greatbelt_chip_get_ecc_recover_en(uint8 lchip);

extern int32
sys_greatbelt_chip_show_ecc_recover_status(uint8 lchip, uint8 is_all);

extern int32
sys_greatbelt_set_chip_global_cfg(uint8 lchip, ctc_chip_global_cfg_t* chip_cfg);

extern int32
sys_greatbelt_get_chip_clock(uint8 lchip, uint16* freq);

extern int32
sys_greatbelt_get_chip_cpumac(uint8 lchip, uint8* mac_sa, uint8* mac_da);

extern int32
sys_greatbelt_chip_get_cpu_eth_en(uint8 lchip, uint8 *enable);

extern int32
sys_greatbelt_chip_set_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para);

extern int32
sys_greatbelt_chip_get_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para);

extern int32
sys_greatbelt_chip_get_phy_mdio(uint8 lchip, uint16 gport, uint8* mdio_bus, uint8* phy_addr);

extern int32
sys_greatbelt_chip_get_lport(uint8 lchip, uint8 mdio_bus, uint8 phy_addr, uint16* lport);

extern int32
sys_greatbelt_chip_write_gephy_reg(uint8 lchip, uint16 gport, ctc_chip_gephy_para_t* gephy_para);

extern int32
sys_greatbelt_chip_read_gephy_reg(uint8 lchip, uint16 gport, ctc_chip_gephy_para_t* gephy_para);

extern int32
sys_greatbelt_chip_write_xgphy_reg(uint8 lchip, uint16 gport, ctc_chip_xgphy_para_t* xgphy_para);

extern int32
sys_greatbelt_chip_read_xgphy_reg(uint8 lchip, uint16 gport, ctc_chip_xgphy_para_t* xgphy_para);

extern int32
sys_greatbelt_chip_set_gephy_scan_special_reg(uint8 lchip, ctc_chip_ge_opt_reg_t* p_gephy_opt, bool enable);

extern int32
sys_greatbelt_chip_set_xgphy_scan_special_reg(uint8 lchip, ctc_chip_xg_opt_reg_t* p_xgphy_opt, uint8 enable);

extern int32
sys_greatbelt_chip_set_phy_scan_para(uint8 lchip, ctc_chip_phy_scan_ctrl_t* p_scan_para);

extern int32
sys_greatbelt_chip_set_phy_scan_en(uint8 lchip, bool enable);

extern int32
sys_greatbelt_chip_set_i2c_scan_para(uint8 lchip, ctc_chip_i2c_scan_t* p_i2c_para);

extern int32
sys_greatbelt_chip_set_i2c_scan_en(uint8 lchip, bool enable);

extern int32
sys_greatbelt_chip_read_i2c_buf(uint8 lchip, ctc_chip_i2c_scan_read_t* p_i2c_scan_read);

extern int32
sys_greatbelt_chip_i2c_read(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para);

extern int32
sys_greatbelt_chip_i2c_write(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para);

extern int32
sys_greatbelt_chip_set_mac_led_mode(uint8 lchip, ctc_chip_led_para_t* p_led_para, ctc_chip_mac_led_type_t led_type, uint8 inner);

extern int32
sys_greatbelt_chip_set_mac_led_mapping(uint8 lchip, ctc_chip_mac_led_mapping_t* p_led_map);

extern int32
sys_greatbelt_chip_set_mac_led_en(uint8 lchip, bool enable);

extern int32
sys_greatbelt_chip_set_gpio_mode(uint8 lchip, uint8 gpio_id, ctc_chip_gpio_mode_t mode);

extern int32
sys_greatbelt_chip_set_gpio_output(uint8 lchip, uint8 gpio_id, uint8 out_para);

extern int32
sys_greatbelt_chip_set_access_type(uint8 lchip, ctc_chip_access_type_t access_type);

extern int32
sys_greatbelt_chip_get_access_type(uint8 lchip, ctc_chip_access_type_t* p_access_type);

extern int32
sys_greatbelt_chip_hss12g_link_power_up(uint8 lchip, uint8 hssid, uint8 link_idx);

extern int32
sys_greatbelt_chip_hss12g_link_power_down(uint8 lchip, uint8 hssid, uint8 link_idx);

/* return value units: MHz*/
extern uint32
sys_greatbelt_get_core_freq(uint8 lchip);

int32
sys_greatbelt_datapath_sim_init(uint8 lchip);

int32
sys_greatbelt_chip_set_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info);

int32
sys_greatbelt_parse_datapath_file(uint8 lchip, char* datapath_config_file);

int32
sys_greatbelt_init_pll_hss(uint8 lchip);

int32
sys_greatbelt_chip_mdio_read(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para);

int32
sys_greatbelt_chip_mdio_write(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para);

int32
sys_greatbelt_chip_set_hss12g_enable(uint8 lchip, uint8 hssid, ctc_chip_serdes_mode_t mode, uint8 enable);


/*cut through*/
extern int32
sys_greatbelt_get_cut_through_speed(uint8 lchip, uint16 gport);

extern int32
sys_greatbelt_get_cut_through_en(uint8 lchip);

extern int32
sys_greatbelt_chip_set_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16 freq);

extern int32
sys_greatbelt_chip_get_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16* freq);

extern int32
sys_greatbelt_get_chip_sensor(uint8 lchip, ctc_chip_sensor_type_t type, uint32* p_value);

extern int32
sys_greatbelt_chip_set_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value);

extern int32
sys_greatbelt_chip_reset_dfe(uint8 lchip, uint8 serdes_id, uint8 enable);

extern int32
sys_greatbelt_chip_reset_rx_serdes(uint8 lchip, uint8 serdes_id, uint8 reset);

extern int32
sys_greatbelt_chip_set_force_signal_detect(uint8 lchip, bool enable);

extern int32
sys_greatbelt_chip_reset_mdio(uint8 lchip);

extern int32
sys_greatbelt_chip_get_device_info(uint8 lchip, ctc_chip_device_info_t* p_device_info);

extern int32
sys_greatbelt_chip_get_net_tx_cal_10g_entry(uint8 lchip, uint8* value);

extern int32
sys_greatbelt_chip_device_check(uint8 lchip);

extern int32
sys_greatbelt_chip_intlk_register_init(uint8 lchip, uint8 intlk_mode);

extern int32
sys_greatbelt_chip_reset_rx_hss(uint8 lchip, uint8 hss_id, uint8 reset);

extern int32
sys_greatbelt_chip_get_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value);

extern int32
sys_greatbelt_chip_set_serdes_prbs_tx(uint8 lchip, ctc_chip_serdes_prbs_t* p_prbs);

extern bool
sys_greatbelt_chip_check_feature_support(uint8 lchip, uint8 feature);

extern int32
sys_greatbelt_chip_set_serdes_tx_en(uint8 lchip, uint16 serdes_id, bool enable);

extern int32
sys_greatbelt_get_gb_gg_interconnect_en(uint8 lchip);

extern int32
sys_greatbelt_chip_get_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info);

extern int32
sys_greatbelt_chip_serdes_tx_en_with_mac(uint8 lchip, uint8 mac_id);

#ifdef __cplusplus
}
#endif

#endif

