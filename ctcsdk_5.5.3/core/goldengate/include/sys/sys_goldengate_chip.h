/**
 @file sys_goldengate_chip.h

 @date 2009-10-19

 @version v2.0

 The file contains all chip related APIs of sys layer
*/

#ifndef _SYS_GOLDENGATE_CHIP_H
#define _SYS_GOLDENGATE_CHIP_H
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
#include "sys_goldengate_chip_global.h"
#include "sys_goldengate_common.h"

/****************************************************************
*
* Defines and Macros
*
****************************************************************/
#define SYS_CHIP_HSS12G_MACRO_NUM        4
#define STS_CHIP_HSS12G_LINK_NUM         8
#define SYS_HSS12G_PORT_EN_REG1          6
#define SYS_HSS12G_PORT_EN_REG2          7
#define SYS_HSS12G_PORT_RESET_REG1       8
#define SYS_HSS12G_PORT_RESET_REG2       9
#define SYS_HSS12G_PLL_COMMON_HIGH_ADDR  16
#define SYS_CHIP_CPU_MAC_ETHER_TYPE      0x5a5a
#define SYS_CHIP_SWITH_THREADHOLD        20
#define SYS_CHIP_PER_SLICE_PORT_NUM      256


#define SYS_DRV_GCHIP_LENGTH                5           /**< Gchip id length(unit: bit) */
#define SYS_DRV_GCHIP_MASK                  0x1F        /**< Gchip id mask(unit: bit) */
#define SYS_DRV_LOCAL_PORT_LENGTH           9           /**< Local port id length(unit: bit) */
#define SYS_DRV_LOCAL_PORT_MASK             0x1FF        /**< Local port mask */

#define SYS_DRV_GPORT_TO_GCHIP(gport)        (((gport) >> SYS_DRV_LOCAL_PORT_LENGTH) & CTC_GCHIP_MASK)
#define SYS_DRV_GPORT_TO_LPORT(gport)        ((gport)& SYS_DRV_LOCAL_PORT_MASK)
#define SYS_DRV_IS_LINKAGG_PORT(gport)       ((SYS_DRV_GPORT_TO_GCHIP(gport)&SYS_DRV_GCHIP_MASK) == CTC_LINKAGG_CHIPID)

#define SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport) (lport)

#define SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport)  (lport)

#define SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport) SYS_MAP_SYS_LPORT_TO_DRV_LPORT(CTC_MAP_GPORT_TO_LPORT((gport)))

#define SYS_MAP_DRV_LPORT_TO_CTC_GPORT(gchip, lport) CTC_MAP_LPORT_TO_GPORT(gchip, SYS_MAP_DRV_LPORT_TO_SYS_LPORT(lport))

#define SYS_MAP_DRV_LPORT_TO_SLICE(lport) (((lport) < SYS_CHIP_PER_SLICE_PORT_NUM) ? 0 : 1)

#define SYS_MAP_CHANID_TO_SLICE(chan) ((chan >= 64) ? 1 : 0)

#define SYS_MAP_CTC_GPORT_TO_DRV_LPORT_WITH_CHECK(gport, lchip, lport) \
{\
    uint8 gchip = SYS_MAP_CTC_GPORT_TO_GCHIP(gport);\
    SYS_GLOBAL_CHIPID_CHECK(gchip);\
    if (FALSE == sys_goldengate_chip_is_local(lchip, gchip)) \
        return CTC_E_CHIP_IS_REMOTE;\
    lport = CTC_MAP_GPORT_TO_LPORT(gport);\
    if (lport >= SYS_GG_MAX_PORT_NUM_PER_CHIP)\
        return CTC_E_INVALID_LOCAL_PORT;\
     lport = SYS_MAP_SYS_LPORT_TO_DRV_LPORT(lport);\
}

#define SYS_MAP_CTC_GPORT_TO_DRV_GPORT(gport) ((CTC_MAP_GPORT_TO_GCHIP(gport) << SYS_DRV_LOCAL_PORT_LENGTH)\
                                              | (CTC_IS_LINKAGG_PORT(gport) ? CTC_MAP_GPORT_TO_LPORT(gport) : SYS_MAP_CTC_GPORT_TO_DRV_LPORT(gport)))

#define SYS_MAP_CTC_GPORT_TO_GCHIP(gport)     (((gport) >> CTC_LOCAL_PORT_LENGTH) & CTC_GCHIP_MASK)

#define SYS_MAP_DRV_GPORT_TO_CTC_GPORT(gport)   (CTC_MAP_LPORT_TO_GPORT(SYS_DRV_GPORT_TO_GCHIP(gport)\
        , SYS_DRV_IS_LINKAGG_PORT(gport)? (SYS_DRV_GPORT_TO_LPORT(gport)):(SYS_MAP_DRV_LPORT_TO_SYS_LPORT(SYS_DRV_GPORT_TO_LPORT(gport)))))

#define SYS_MAX_PHY_PORT_CHECK(lport) \
{ \
    if (((lport) & 0xff) >= 64) \
    {  \
        return CTC_E_INVALID_LOCAL_PORT;  \
    } \
}

#define SYS_GCHIP_IS_REMOTE(lchip, gchip) ((0x1F != gchip)&&(!sys_goldengate_chip_is_local(lchip, gchip)))


/*Below Define is for CTC APIs dispatch*/
/*---------------------------------------------------------------*/
extern uint8 g_ctcs_api_en;

#define SYS_MAP_GCHIP_TO_LCHIP(gchip, lchip) \
    do{\
       SYS_GLOBAL_CHIPID_CHECK(gchip);\
        if(!g_ctcs_api_en)\
        {\
            if(CTC_E_NONE != sys_goldengate_get_local_chip_id(gchip, &lchip))\
            {\
                return CTC_E_CHIP_IS_REMOTE;\
            }\
        }\
    }while(0);

#define SYS_GG_MAX_LOCAL_CHIP_NUM               30
#define SYS_GG_MAX_PHY_PORT                     96
#define SYS_GG_MAX_VRFID_ID                     255
#define SYS_GG_MAX_LINKAGG_GROUP_NUM            64
#define SYS_GG_MAX_PORT_NUM_PER_CHIP            512
#define SYS_GG_MAX_ECPN                         64
#define SYS_GG_MIN_CTC_L3IF_ID                  1
#define SYS_GG_MAX_CTC_L3IF_ID                  1022
#define SYS_GG_MAX_APS_GROUP_NUM                2048
#define SYS_GG_MAX_DEFAULT_ECMP_NUM             16
#define SYS_GG_MAX_ACL_PORT_BITMAP_NUM          128
#define SYS_GG_MAX_ACL_PORT_CLASSID             255
#define SYS_GG_MIN_ACL_PORT_CLASSID             1
#define SYS_GG_MAX_SCL_PORT_CLASSID             255
#define SYS_GG_MIN_SCL_PORT_CLASSID             1
#define SYS_GG_MAX_ACL_VLAN_CLASSID             255
#define SYS_GG_MIN_ACL_VLAN_CLASSID             1
#define SYS_GG_PKT_CPUMAC_LEN                   18
#define SYS_GG_PKT_HEADER_LEN                   40
#define SYS_GG_LPORT_CPU                        0x10000000
#define SYS_GG_MAX_CPU_MACDA_NUM                4
#define SYS_GG_MAX_LINKAGG_ID                   0x3F
#define SYS_GG_MAX_CPU_REASON_GROUP_NUM         16
#define SYS_GG_MAX_QNUM_PER_GROUP               8
#define SYS_GG_MAX_INTR_GROUP                   6
#define SYS_GG_LINKAGGID_MASK                   0x1F
#define SYS_GG_MAX_IPUC_IP_TUNNEL_IF_NUM        4
#define SYS_GG_DATAPATH_SERDES_NUM              96
#define SYS_GG_IPMC_RP_INTF                     16
#define SYS_GG_MAX_IPMC_RPF_IF                  16
#define SYS_GG_CHIP_FFE_PARAM_NUM               4
#define SYS_GG_MAX_GCHIP_CHIP_ID                0x1E

#define SYS_GG_MLAG_ISOLATION_GROUP             31
#define SYS_GG_MAX_RESERVED_PORT                32
#define SYS_GG_MAX_NORMAL_PANNEL_PORT           96
#define SYS_GG_MAX_EXTEND_PANNEL_PORT           384
#define SYS_GG_NORMAL_PANNEL_PORT_BASE0         0
#define SYS_GG_NORMAL_PANNEL_PORT_BASE1         64
#define SYS_GG_EXTEND_PANNEL_PORT_BASE0         128
#define SYS_GG_EXTEND_PANNEL_PORT_BASE1         320

#define SYS_ACL_VLAN_CLASS_ID_CHECK(id) \
    if (((id) < SYS_GG_MIN_ACL_VLAN_CLASSID) || ((id) > SYS_GG_MAX_ACL_VLAN_CLASSID)) \
    { \
        return CTC_E_INVALID_PARAM; \
    }

#define SYS_ACL_PORT_CLASS_ID_CHECK(id) \
    if (((id) < SYS_GG_MIN_ACL_PORT_CLASSID) || ((id) > SYS_GG_MAX_ACL_PORT_CLASSID)) \
    { \
        return CTC_E_INVALID_PARAM; \
    }

#define SYS_SCL_PORT_CLASS_ID_CHECK(id) \
    if (((id) < SYS_GG_MIN_SCL_PORT_CLASSID) || ((id) > SYS_GG_MAX_SCL_PORT_CLASSID)) \
    { \
        return CTC_E_INVALID_PARAM; \
    }


#define SYS_LOCAL_CHIPID_CHECK(lchip)                                      \
    do {                                                                   \
        uint8 local_chip_num = sys_goldengate_get_local_chip_num();        \
        if (lchip >= local_chip_num){ return CTC_E_INVALID_LOCAL_CHIPID; } \
    } while (0);


#define FEATURE_SUPPORT_CHECK(feature) \
{\
    if (CTC_FEATURE_MAX <= feature) \
        return CTC_E_INTR_INVALID_PARAM; \
    if (FALSE == sys_goldengate_chip_check_feature_support(feature)) \
        return CTC_E_NOT_SUPPORT;\
}


#define SYS_CHIP_DBG_OUT(level, FMT, ...) \
    do { \
        CTC_DEBUG_OUT(chip, chip, CHIP_SYS, level, FMT, ##__VA_ARGS__); \
    } while (0)

/*#define SYS_HUMBER_CHIP_CLOCK 625*/
#define CHIP_LOCK(lchip) \
    if (p_gg_chip_master[lchip]->p_chip_mutex) sal_mutex_lock(p_gg_chip_master[lchip]->p_chip_mutex)

#define CHIP_UNLOCK(lchip) \
    if (p_gg_chip_master[lchip]->p_chip_mutex) sal_mutex_unlock(p_gg_chip_master[lchip]->p_chip_mutex)

#define SWITCH_LOCK(lchip) \
    if (p_gg_chip_master[lchip]->p_switch_mutex) sal_mutex_lock(p_gg_chip_master[lchip]->p_switch_mutex)

#define SWITCH_UNLOCK(lchip) \
    if (p_gg_chip_master[lchip]->p_switch_mutex) sal_mutex_unlock(p_gg_chip_master[lchip]->p_switch_mutex)

/*#define LCHIP_CHECK(lchip)                              \
    do {                                                     \
        if ((lchip) >= sys_goldengate_get_local_chip_num()){  \
            return CTC_E_INVALID_LOCAL_CHIPID; }              \
    } while (0)*/

#define SYS_LPORT_IS_CPU_ETH(lchip, lport) sys_goldengate_chip_is_eth_cpu_port(lchip, lport)


#define SYS_HSS12G_ADDRESS_CONVERT(addr_h, addr_l)   (((addr_h) << 6) | (addr_l))

#define SYS_XQMAC_PER_SLICE_NUM      12
#define SYS_SUBMAC_PER_XQMAC_NUM      4
#define SYS_SGMAC_PER_SLICE_NUM       (SYS_XQMAC_PER_SLICE_NUM * SYS_SUBMAC_PER_XQMAC_NUM)
#define SYS_CGMAC_PER_SLICE_NUM       2

#define SYS_CHIP_MAC_LED_CLK    25

enum sys_chip_serdes_overclocking_speed_e
{
    SYS_CHIP_SERDES_OVERCLOCKING_SPEED_NONE_MODE = 0,
    SYS_CHIP_SERDES_OVERCLOCKING_SPEED_11G_MODE = 11,
    SYS_CHIP_SERDES_OVERCLOCKING_SPEED_12DOT5G_MODE = 12,

    SYS_CHIP_MAX_SERDES_OVERCLOCKING_SPEED_MODE
};
typedef enum sys_chip_serdes_overclocking_speed_e sys_chip_serdes_overclocking_speed_t;

struct sys_chip_master_s
{
    sal_mutex_t* p_chip_mutex;
    sal_mutex_t* p_switch_mutex;
    uint8   resv[2];
    uint8   gb_gg_interconnect_en;
    uint8   g_chip_id;
    char    datapath_mode[20];
    uint8   first_ge_opt_reg_used;
    uint8   first_ge_opt_reg;
    uint8   second_ge_opt_reg_used;
    uint8   second_ge_opt_reg;
    mac_addr_t cpu_mac_sa;
    mac_addr_t cpu_mac_da[SYS_GG_MAX_CPU_MACDA_NUM];
    uint8   port_mdio_mapping_tbl[SYS_GG_MAX_PHY_PORT];
    uint8   port_phy_mapping_tbl[SYS_GG_MAX_PHY_PORT];

    uint8   cut_through_en;
    uint8   cut_through_speed;

    uint16   tpid;
    uint8   cpu_eth_en;
    uint16  cpu_channel;
    uint16  vlan_id;

    sal_mutex_t* p_i2c_mutex;
    sal_mutex_t* p_smi_mutex;
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

enum sys_chip_device_id_type_e
{
    SYS_CHIP_DEVICE_ID_GG_CTC8096,
    SYS_CHIP_DEVICE_ID_INVALID,
};
typedef enum sys_chip_device_id_type_e sys_chip_device_id_type_t;

/****************************************************************************
 *
* Function
*
*****************************************************************************/
extern int32
sys_goldengate_chip_init(uint8 lchip, uint8 lchip_num);

/**
 @brief De-Initialize chip module
*/
extern int32
sys_goldengate_chip_deinit(uint8 lchip);

extern uint8
sys_goldengate_get_local_chip_num(void);

extern int32
sys_goldengate_set_gchip_id(uint8 lchip, uint8 gchip_id);

extern bool
sys_goldengate_chip_is_local(uint8 lchip, uint8 gchip_id);

extern int32
sys_goldengate_get_gchip_id(uint8 lchip, uint8* gchip_id);

extern int32
sys_goldengate_set_chip_global_cfg(uint8 lchip, ctc_chip_global_cfg_t* chip_cfg);

extern int32
sys_goldengate_get_chip_clock(uint8 lchip, uint16* freq);

extern int32
sys_goldengate_get_chip_cpumac(uint8 lchip, uint8* mac_sa, uint8* mac_da);

extern int32
sys_goldengate_get_chip_cpu_eth_en(uint8 lchip, uint8 *enable, uint8* cpu_eth_chan);

extern int32
sys_goldengate_chip_set_eth_cpu_cfg(uint8 lchip);

extern bool
sys_goldengate_chip_is_eth_cpu_port(uint8 lchip, uint16 lport);

extern int32
sys_goldengate_chip_set_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para);

extern int32
sys_goldengate_chip_get_phy_mapping(uint8 lchip, ctc_chip_phy_mapping_para_t* phy_mapping_para);

extern int32
sys_goldengate_chip_get_phy_mdio(uint8 lchip, uint16 gport, uint8* mdio_bus, uint8* phy_addr);

extern int32
sys_goldengate_chip_write_gephy_reg(uint8 lchip, uint16 gport, ctc_chip_gephy_para_t* gephy_para);

extern int32
sys_goldengate_chip_read_gephy_reg(uint8 lchip, uint16 gport, ctc_chip_gephy_para_t* gephy_para);

extern int32
sys_goldengate_chip_write_xgphy_reg(uint8 lchip, uint16 gport, ctc_chip_xgphy_para_t* xgphy_para);

extern int32
sys_goldengate_chip_read_xgphy_reg(uint8 lchip, uint16 gport, ctc_chip_xgphy_para_t* xgphy_para);

extern int32
sys_goldengate_chip_set_gephy_scan_special_reg(uint8 lchip, ctc_chip_ge_opt_reg_t* p_gephy_opt, bool enable);

extern int32
sys_goldengate_chip_set_xgphy_scan_special_reg(uint8 lchip, ctc_chip_xg_opt_reg_t* p_xgphy_opt, uint8 enable);

extern int32
sys_goldengate_chip_set_phy_scan_para(uint8 lchip, ctc_chip_phy_scan_ctrl_t* p_scan_para);

extern int32
sys_goldengate_chip_set_phy_scan_en(uint8 lchip, bool enable);

extern int32
sys_goldengate_chip_set_i2c_scan_para(uint8 lchip, ctc_chip_i2c_scan_t* p_i2c_para);

extern int32
sys_goldengate_chip_set_i2c_scan_en(uint8 lchip, bool enable);

extern int32
sys_goldengate_chip_read_i2c_buf(uint8 lchip, ctc_chip_i2c_scan_read_t *p_i2c_scan_read);

extern int32
sys_goldengate_chip_i2c_read(uint8 lchip, ctc_chip_i2c_read_t* p_i2c_para);

extern int32
sys_goldengate_chip_i2c_write(uint8 lchip, ctc_chip_i2c_write_t* p_i2c_para);

extern int32
sys_goldengate_chip_set_mac_led_mode(uint8 lchip, ctc_chip_led_para_t* p_led_para, ctc_chip_mac_led_type_t led_type, uint8 inner);

extern int32
sys_goldengate_chip_set_mac_led_mapping(uint8 lchip, ctc_chip_mac_led_mapping_t* p_led_map);

extern int32
sys_goldengate_chip_set_mac_led_en(uint8 lchip, bool enable);

extern int32
sys_goldengate_chip_set_gpio_mode(uint8 lchip, uint8 gpio_id, ctc_chip_gpio_mode_t mode);

extern int32
sys_goldengate_chip_set_gpio_output(uint8 lchip, uint8 gpio_id, uint8 out_para);

extern int32
sys_goldengate_chip_get_gpio_input(uint8 lchip, uint8 gpio_id, uint8* in_value);

extern int32
sys_goldengate_chip_set_access_type(uint8 lchip, ctc_chip_access_type_t access_type);

extern int32
sys_goldengate_chip_get_access_type(uint8 lchip, ctc_chip_access_type_t* p_access_type);

extern int32
sys_goldengate_chip_hss12g_link_power_up(uint8 lchip, uint8 hssid, uint8 link_idx);

extern int32
sys_goldengate_chip_hss12g_link_power_down(uint8 lchip, uint8 hssid, uint8 link_idx);

extern int32
sys_goldengate_chip_get_serdes_info(uint8 lchip, uint8 lport, uint8* macro_idx, uint8* link_idx);

/* return value units: MHz*/
extern uint16
sys_goldengate_get_core_freq(uint8 lchip, uint8 type);

extern int32
sys_goldengate_chip_set_serdes_mode(uint8 lchip, ctc_chip_serdes_info_t* p_serdes_info);

extern int32
sys_goldengate_chip_mdio_read(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para);

extern int32
sys_goldengate_chip_mdio_write(uint8 lchip, ctc_chip_mdio_type_t type, ctc_chip_mdio_para_t* p_para);

extern int32
sys_goldengate_chip_set_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16 freq);

extern int32
sys_goldengate_chip_set_hss12g_enable(uint8 lchip, uint8 hssid, ctc_chip_serdes_mode_t mode, uint8 enable);

extern int32
sys_goldengate_get_cut_through_en(uint8 lchip);

extern int32
sys_goldengate_get_cut_through_speed(uint8 lchip, uint16 gport);

extern int32
sys_goldengate_chip_set_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value);

extern int32
sys_goldengate_get_chip_sensor(uint8 lchip, ctc_chip_sensor_type_t type, uint32* p_value);

extern int32
sys_goldengate_chip_ecc_recover_init(uint8 lchip);

extern int32
sys_goldengate_chip_show_ecc_recover_status(uint8 lchip, uint8 is_all);

extern int32
sys_goldengate_chip_get_device_info(uint8 lchip, ctc_chip_device_info_t* p_device_info);

extern int32
sys_goldengate_mdio_init(uint8 lchip);

extern int32
sys_goldengate_chip_get_property(uint8 lchip, ctc_chip_property_t chip_prop, void* p_value);

extern int32
sys_goldengate_chip_set_dlb_chan_type(uint8 lchip, uint8 chan_id);

/**
 Configure the tcam scan burst interval time and burst entry num.
 When burst_interval = 0, means scan all tcam without interval, otherwise burst_entry_num must > 0.
 */
extern int32
sys_goldengate_chip_set_tcam_scan_cfg(uint8 lchip, uint32 burst_entry_num, uint32 burst_interval);

extern int32
sys_goldengate_get_local_chip_id(uint8 gchip_id, uint8* lchip_id);

extern bool
sys_goldengate_chip_check_feature_support(uint8 feature);

extern int32
sys_goldengate_get_gb_gg_interconnect_en(uint8 lchip);

extern
int32 sys_goldengate_chip_read_pcie_phy_register(uint8 lchip, uint32 addr, uint32* p_value);

extern
int32 sys_goldengate_chip_write_pcie_phy_register(uint8 lchip, uint32 addr, uint32 value);

extern int32
sys_goldengate_chip_get_mdio_clock(uint8 lchip, ctc_chip_mdio_type_t type, uint16* p_freq);

extern int32
sys_goldengate_chip_get_pcie_select(uint8 lchip, uint8* pcie_sel);
#ifdef __cplusplus
}
#endif

#endif

